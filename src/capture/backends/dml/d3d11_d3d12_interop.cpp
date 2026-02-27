#include "capture/backends/dml/d3d11_d3d12_interop.hpp"

#include <cstdint>
#include <expected>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/backends/dml/dx_utils.hpp"

#ifdef _WIN32
#include <DirectML.h>
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#endif

namespace vf {

#ifdef _WIN32
namespace {

std::expected<void, std::error_code> toError(std::expected<void, dx_utils::DxCallError> result,
                                             CaptureError errorCode) {
    if (result) {
        return {};
    }

    const dx_utils::DxCallError err = result.error();
    if (err.isWin32) {
        VF_ERROR("{} failed (Win32Error=0x{:08X})", err.apiName,
                 static_cast<std::uint32_t>(HRESULT_CODE(err.hr)));
    } else {
        VF_ERROR("{} failed (HRESULT=0x{:08X})", err.apiName, static_cast<std::uint32_t>(err.hr));
    }
    return std::unexpected(makeErrorCode(errorCode));
}

} // namespace

class D3d11D3d12Interop::Impl {
  public:
    std::expected<UpdateResult, std::error_code>
    initializeOrUpdate(ID3D11Texture2D* sourceTexture) {
        if (sourceTexture == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        winrt::com_ptr<ID3D11Device> incomingDevice;
        sourceTexture->GetDevice(incomingDevice.put());
        if (incomingDevice == nullptr) {
            VF_ERROR("ID3D11Texture2D::GetDevice returned null device");
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        D3D11_TEXTURE2D_DESC incomingDesc{};
        sourceTexture->GetDesc(&incomingDesc);

        const TransitionDecision transition = decideTransition(incomingDesc, incomingDevice.get());
        if (transition.reuseCurrentState) {
            return buildUpdateResult(false, false);
        }
        if (transition.requiresReset) {
            reset();
        }

        bool commit = false;
        auto rollback = dx_utils::makeScopeExit([this, &commit]() {
            if (!commit) {
                reset();
            }
        });

        const auto d3d11Result = initializeD3d11State(incomingDevice, incomingDesc);
        if (!d3d11Result) {
            return std::unexpected(d3d11Result.error());
        }

        const auto d3d12Result = initializeD3d12AndDmlState();
        if (!d3d12Result) {
            return std::unexpected(d3d12Result.error());
        }

        const auto sharedInputResult = initializeSharedInputState();
        if (!sharedInputResult) {
            return std::unexpected(sharedInputResult.error());
        }

        const auto interopFenceResult = initializeInteropFenceState();
        if (!interopFenceResult) {
            return std::unexpected(interopFenceResult.error());
        }

        lastInteropFenceValue = 0;
        if (generationId == 0) {
            generationId = 1;
        } else if (transition.reinitialized) {
            ++generationId;
        }
        initialized = true;
        commit = true;
        rollback.dismiss();
        VF_DEBUG("D3d11D3d12Interop initialized for {}x{} format={}", sourceDesc.Width,
                 sourceDesc.Height, static_cast<std::uint32_t>(sourceDesc.Format));
        return buildUpdateResult(transition.reinitialized, transition.sourceChanged);
    }

    std::expected<std::uint64_t, std::error_code> copyAndSignal(ID3D11Texture2D* frameTexture,
                                                                std::uint64_t requestedFenceValue) {
        if (!initialized || frameTexture == nullptr || sharedInputTextureD3d11 == nullptr ||
            d3d11Context == nullptr || d3d11Context4 == nullptr || interopFenceD3d11 == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        d3d11Context->CopyResource(sharedInputTextureD3d11.get(), frameTexture);

        std::uint64_t effectiveFenceValue = requestedFenceValue;
        if (effectiveFenceValue <= lastInteropFenceValue) {
            effectiveFenceValue = lastInteropFenceValue + 1;
        }
        lastInteropFenceValue = effectiveFenceValue;

        const auto signalResult = toError(
            dx_utils::checkD3d(d3d11Context4->Signal(interopFenceD3d11.get(), effectiveFenceValue),
                               "ID3D11DeviceContext4::Signal(sharedInput)"),
            CaptureError::InferenceGpuInteropFailed);
        if (!signalResult) {
            return std::unexpected(signalResult.error());
        }

        return effectiveFenceValue;
    }

    std::expected<void, std::error_code> waitOnQueue(std::uint64_t fenceValue) const {
        if (!initialized || d3d12Queue == nullptr || interopFenceD3d12 == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto waitResult =
            toError(dx_utils::checkD3d(d3d12Queue->Wait(interopFenceD3d12.get(), fenceValue),
                                       "ID3D12CommandQueue::Wait(sharedInput)"),
                    CaptureError::InferenceGpuInteropFailed);
        if (!waitResult) {
            return std::unexpected(waitResult.error());
        }

        return {};
    }

    void reset() {
        interopFenceD3d11 = nullptr;
        interopFenceD3d12 = nullptr;
        sharedInputTextureD3d12 = nullptr;
        sharedInputTextureD3d11 = nullptr;
        dmlDevice = nullptr;
        d3d12Queue = nullptr;
        d3d12Device = nullptr;
        d3d11Context4 = nullptr;
        d3d11Device5 = nullptr;
        d3d11Context = nullptr;
        d3d11Device = nullptr;

        interopFenceHandle.reset();
        sharedTextureHandle.reset();
        if (directmlModule != nullptr) {
            static_cast<void>(FreeLibrary(directmlModule));
            directmlModule = nullptr;
        }

        sourceDesc = {};
        lastInteropFenceValue = 0;
        initialized = false;
    }

    [[nodiscard]] ID3D12Device* getD3d12Device() const { return d3d12Device.get(); }
    [[nodiscard]] ID3D12CommandQueue* getD3d12Queue() const { return d3d12Queue.get(); }
    [[nodiscard]] IDMLDevice* getDmlDevice() const { return dmlDevice.get(); }
    [[nodiscard]] ID3D12Resource* getSharedInputTextureD3d12() const {
        return sharedInputTextureD3d12.get();
    }
    [[nodiscard]] const D3D11_TEXTURE2D_DESC& getSourceDescRef() const { return sourceDesc; }

  private:
    struct TransitionDecision {
        bool reuseCurrentState = false;
        bool requiresReset = false;
        bool reinitialized = false;
        bool sourceChanged = false;
    };

    [[nodiscard]] TransitionDecision decideTransition(const D3D11_TEXTURE2D_DESC& incomingDesc,
                                                      ID3D11Device* incomingDevice) const {
        if (!initialized) {
            return TransitionDecision{
                .reuseCurrentState = false,
                .requiresReset = false,
                .reinitialized = true,
                .sourceChanged = false,
            };
        }

        if (canReuseState(incomingDesc, incomingDevice)) {
            return TransitionDecision{
                .reuseCurrentState = true,
                .requiresReset = false,
                .reinitialized = false,
                .sourceChanged = false,
            };
        }

        return TransitionDecision{
            .reuseCurrentState = false,
            .requiresReset = true,
            .reinitialized = true,
            .sourceChanged = true,
        };
    }

    [[nodiscard]] bool canReuseState(const D3D11_TEXTURE2D_DESC& incomingDesc,
                                     ID3D11Device* incomingDevice) const {
        const bool sameFormat = incomingDesc.Width == sourceDesc.Width &&
                                incomingDesc.Height == sourceDesc.Height &&
                                incomingDesc.Format == sourceDesc.Format;
        const bool sameDevice = incomingDevice == d3d11Device.get();
        return sameFormat && sameDevice;
    }

    [[nodiscard]] UpdateResult buildUpdateResult(bool reinitializedState,
                                                 bool sourceChangedState) const {
        return UpdateResult{
            .generationId = generationId,
            .reinitialized = reinitializedState,
            .sourceChanged = sourceChangedState,
            .device = d3d12Device.get(),
            .commandQueue = d3d12Queue.get(),
            .dmlDevice = dmlDevice.get(),
            .sharedInputTexture = sharedInputTextureD3d12.get(),
            .sourceWidth = sourceDesc.Width,
            .sourceHeight = sourceDesc.Height,
            .sourceFormat = static_cast<std::uint32_t>(sourceDesc.Format),
        };
    }

    std::expected<void, std::error_code>
    initializeD3d11State(const winrt::com_ptr<ID3D11Device>& incomingDevice,
                         const D3D11_TEXTURE2D_DESC& incomingDesc) {
        d3d11Device = incomingDevice;
        sourceDesc = incomingDesc;

        d3d11Device->GetImmediateContext(d3d11Context.put());
        if (d3d11Context == nullptr) {
            VF_ERROR("ID3D11Device::GetImmediateContext returned null context");
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto queryDevice5Result =
            toError(dx_utils::checkD3d(d3d11Device->QueryInterface(__uuidof(ID3D11Device5),
                                                                   d3d11Device5.put_void()),
                                       "ID3D11Device::QueryInterface(ID3D11Device5)"),
                    CaptureError::InferenceInitializationFailed);
        if (!queryDevice5Result) {
            return std::unexpected(queryDevice5Result.error());
        }

        const auto queryContext4Result =
            toError(dx_utils::checkD3d(d3d11Context->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                                                    d3d11Context4.put_void()),
                                       "ID3D11DeviceContext::QueryInterface(ID3D11DeviceContext4)"),
                    CaptureError::InferenceInitializationFailed);
        if (!queryContext4Result) {
            return std::unexpected(queryContext4Result.error());
        }

        return {};
    }

    std::expected<void, std::error_code> initializeD3d12AndDmlState() {
        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        const auto queryDxgiDeviceResult =
            toError(dx_utils::checkD3d(
                        d3d11Device->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void()),
                        "ID3D11Device::QueryInterface(IDXGIDevice)"),
                    CaptureError::InferenceInitializationFailed);
        if (!queryDxgiDeviceResult) {
            return std::unexpected(queryDxgiDeviceResult.error());
        }

        winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
        const auto getAdapterResult =
            toError(dx_utils::checkD3d(dxgiDevice->GetAdapter(dxgiAdapter.put()),
                                       "IDXGIDevice::GetAdapter"),
                    CaptureError::InferenceInitializationFailed);
        if (!getAdapterResult) {
            return std::unexpected(getAdapterResult.error());
        }

        const auto createD3d12DeviceResult = toError(
            dx_utils::checkD3d(D3D12CreateDevice(dxgiAdapter.get(), D3D_FEATURE_LEVEL_12_0,
                                                 __uuidof(ID3D12Device), d3d12Device.put_void()),
                               "D3D12CreateDevice"),
            CaptureError::InferenceInitializationFailed);
        if (!createD3d12DeviceResult) {
            return std::unexpected(createD3d12DeviceResult.error());
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 0;
        const auto createQueueResult = toError(
            dx_utils::checkD3d(d3d12Device->CreateCommandQueue(
                                   &queueDesc, __uuidof(ID3D12CommandQueue), d3d12Queue.put_void()),
                               "ID3D12Device::CreateCommandQueue"),
            CaptureError::InferenceInitializationFailed);
        if (!createQueueResult) {
            return std::unexpected(createQueueResult.error());
        }

        directmlModule = LoadLibraryW(L"DirectML.dll");
        const auto loadModuleResult =
            toError(dx_utils::checkWin32(directmlModule != nullptr, "LoadLibraryW(DirectML.dll)"),
                    CaptureError::InferenceInitializationFailed);
        if (!loadModuleResult) {
            return std::unexpected(loadModuleResult.error());
        }

        using DmlCreateDevice1Fn = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS,
                                                    DML_FEATURE_LEVEL, REFIID, void**);
        auto* createDevice1 = reinterpret_cast<DmlCreateDevice1Fn>(
            GetProcAddress(directmlModule, "DMLCreateDevice1"));
        if (createDevice1 == nullptr) {
            VF_ERROR("GetProcAddress(DMLCreateDevice1) failed (Win32Error=0x{:08X})",
                     static_cast<std::uint32_t>(GetLastError()));
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto createDmlDeviceResult =
            toError(dx_utils::checkD3d(createDevice1(d3d12Device.get(), DML_CREATE_DEVICE_FLAG_NONE,
                                                     DML_FEATURE_LEVEL_1_0, __uuidof(IDMLDevice),
                                                     dmlDevice.put_void()),
                                       "DMLCreateDevice1"),
                    CaptureError::InferenceInitializationFailed);
        if (!createDmlDeviceResult) {
            return std::unexpected(createDmlDeviceResult.error());
        }

        return {};
    }

    std::expected<void, std::error_code> initializeSharedInputState() {
        D3D11_TEXTURE2D_DESC sharedInputDesc{};
        sharedInputDesc.Width = sourceDesc.Width;
        sharedInputDesc.Height = sourceDesc.Height;
        sharedInputDesc.MipLevels = sourceDesc.MipLevels;
        sharedInputDesc.ArraySize = sourceDesc.ArraySize;
        sharedInputDesc.Format = sourceDesc.Format;
        sharedInputDesc.SampleDesc = sourceDesc.SampleDesc;
        sharedInputDesc.Usage = D3D11_USAGE_DEFAULT;
        sharedInputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        sharedInputDesc.CPUAccessFlags = 0;
        sharedInputDesc.MiscFlags =
            D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        const auto createSharedTextureResult =
            toError(dx_utils::checkD3d(d3d11Device->CreateTexture2D(&sharedInputDesc, nullptr,
                                                                    sharedInputTextureD3d11.put()),
                                       "ID3D11Device::CreateTexture2D(sharedInputTexture)"),
                    CaptureError::InferenceInitializationFailed);
        if (!createSharedTextureResult) {
            return std::unexpected(createSharedTextureResult.error());
        }

        winrt::com_ptr<IDXGIResource1> dxgiSharedResource;
        const auto querySharedResourceResult =
            toError(dx_utils::checkD3d(sharedInputTextureD3d11->QueryInterface(
                                           __uuidof(IDXGIResource1), dxgiSharedResource.put_void()),
                                       "ID3D11Texture2D::QueryInterface(IDXGIResource1)"),
                    CaptureError::InferenceInitializationFailed);
        if (!querySharedResourceResult) {
            return std::unexpected(querySharedResourceResult.error());
        }

        const auto createSharedTextureHandleResult = toError(
            dx_utils::checkD3d(dxgiSharedResource->CreateSharedHandle(
                                   nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                   nullptr, sharedTextureHandle.put()),
                               "IDXGIResource1::CreateSharedHandle(inputTexture)"),
            CaptureError::InferenceInitializationFailed);
        if (!createSharedTextureHandleResult) {
            return std::unexpected(createSharedTextureHandleResult.error());
        }

        const auto openSharedTextureResult =
            toError(dx_utils::checkD3d(d3d12Device->OpenSharedHandle(
                                           sharedTextureHandle.get(), __uuidof(ID3D12Resource),
                                           sharedInputTextureD3d12.put_void()),
                                       "ID3D12Device::OpenSharedHandle(inputTexture)"),
                    CaptureError::InferenceInitializationFailed);
        if (!openSharedTextureResult) {
            return std::unexpected(openSharedTextureResult.error());
        }

        return {};
    }

    std::expected<void, std::error_code> initializeInteropFenceState() {
        const auto createInteropFenceResult =
            toError(dx_utils::checkD3d(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                                                __uuidof(ID3D12Fence),
                                                                interopFenceD3d12.put_void()),
                                       "ID3D12Device::CreateFence(sharedInterop)"),
                    CaptureError::InferenceInitializationFailed);
        if (!createInteropFenceResult) {
            return std::unexpected(createInteropFenceResult.error());
        }

        const auto createInteropFenceHandleResult =
            toError(dx_utils::checkD3d(d3d12Device->CreateSharedHandle(
                                           interopFenceD3d12.get(), nullptr, GENERIC_ALL, nullptr,
                                           interopFenceHandle.put()),
                                       "ID3D12Device::CreateSharedHandle(sharedInteropFence)"),
                    CaptureError::InferenceInitializationFailed);
        if (!createInteropFenceHandleResult) {
            return std::unexpected(createInteropFenceHandleResult.error());
        }

        const auto openInteropFenceResult =
            toError(dx_utils::checkD3d(d3d11Device5->OpenSharedFence(interopFenceHandle.get(),
                                                                     __uuidof(ID3D11Fence),
                                                                     interopFenceD3d11.put_void()),
                                       "ID3D11Device5::OpenSharedFence(sharedInteropFence)"),
                    CaptureError::InferenceInitializationFailed);
        if (!openInteropFenceResult) {
            return std::unexpected(openInteropFenceResult.error());
        }

        return {};
    }

    bool initialized = false;
    std::uint64_t generationId = 0;
    std::uint64_t lastInteropFenceValue = 0;

    winrt::com_ptr<ID3D11Device> d3d11Device;
    winrt::com_ptr<ID3D11DeviceContext> d3d11Context;
    winrt::com_ptr<ID3D11Device5> d3d11Device5;
    winrt::com_ptr<ID3D11DeviceContext4> d3d11Context4;

    winrt::com_ptr<ID3D12Device> d3d12Device;
    winrt::com_ptr<ID3D12CommandQueue> d3d12Queue;
    winrt::com_ptr<IDMLDevice> dmlDevice;

    winrt::com_ptr<ID3D11Texture2D> sharedInputTextureD3d11;
    winrt::com_ptr<ID3D12Resource> sharedInputTextureD3d12;
    winrt::com_ptr<ID3D11Fence> interopFenceD3d11;
    winrt::com_ptr<ID3D12Fence> interopFenceD3d12;

    D3D11_TEXTURE2D_DESC sourceDesc{};
    dx_utils::UniqueWin32Handle sharedTextureHandle;
    dx_utils::UniqueWin32Handle interopFenceHandle;
    HMODULE directmlModule = nullptr;
};

D3d11D3d12Interop::D3d11D3d12Interop() : impl(std::make_unique<Impl>()) {}
D3d11D3d12Interop::~D3d11D3d12Interop() { impl->reset(); }

std::expected<D3d11D3d12Interop::UpdateResult, std::error_code>
D3d11D3d12Interop::initializeOrUpdate(ID3D11Texture2D* sourceTexture) {
    return impl->initializeOrUpdate(sourceTexture);
}

std::expected<std::uint64_t, std::error_code>
D3d11D3d12Interop::copyAndSignal(ID3D11Texture2D* frameTexture, std::uint64_t requestedFenceValue) {
    return impl->copyAndSignal(frameTexture, requestedFenceValue);
}

std::expected<void, std::error_code> D3d11D3d12Interop::waitOnQueue(std::uint64_t fenceValue) {
    return impl->waitOnQueue(fenceValue);
}

ID3D12Device* D3d11D3d12Interop::getD3d12Device() const { return impl->getD3d12Device(); }
ID3D12CommandQueue* D3d11D3d12Interop::getD3d12Queue() const { return impl->getD3d12Queue(); }
IDMLDevice* D3d11D3d12Interop::getDmlDevice() const { return impl->getDmlDevice(); }
ID3D12Resource* D3d11D3d12Interop::getSharedInputTextureD3d12() const {
    return impl->getSharedInputTextureD3d12();
}
const D3D11_TEXTURE2D_DESC& D3d11D3d12Interop::getSourceDesc() const {
    return impl->getSourceDescRef();
}

void D3d11D3d12Interop::reset() { impl->reset(); }

#else

class D3d11D3d12Interop::Impl {};

D3d11D3d12Interop::D3d11D3d12Interop() : impl(std::make_unique<Impl>()) {}
D3d11D3d12Interop::~D3d11D3d12Interop() = default;

std::expected<D3d11D3d12Interop::UpdateResult, std::error_code>
D3d11D3d12Interop::initializeOrUpdate(void* sourceTexture) {
    static_cast<void>(sourceTexture);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<std::uint64_t, std::error_code>
D3d11D3d12Interop::copyAndSignal(void* frameTexture, std::uint64_t requestedFenceValue) {
    static_cast<void>(frameTexture);
    static_cast<void>(requestedFenceValue);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<void, std::error_code> D3d11D3d12Interop::waitOnQueue(std::uint64_t fenceValue) {
    static_cast<void>(fenceValue);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

void D3d11D3d12Interop::reset() {}

#endif

} // namespace vf
