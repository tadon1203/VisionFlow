#include "inference/platform/dml/dml_image_processor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <string_view>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "inference/platform/dml/dx_utils.hpp"
#include "inference/platform/dml/onnx_dml_session.hpp"

#ifdef _WIN32
#include <DirectML.h>
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#endif

namespace vf {

#ifdef _WIN32
// D3D12 descriptor/view structs are tagged unions in the Win32 API.
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
namespace {

std::expected<void, std::error_code>
toCaptureError(std::expected<void, dx_utils::DxCallError> result, CaptureError errorCode) {
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

constexpr std::string_view kPreprocessShaderSource = R"(
cbuffer PreprocessParams : register(b0)
{
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
};

Texture2D<float4> inputTexture : register(t0);
RWByteAddressBuffer outputTensor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= dstWidth || dispatchThreadId.y >= dstHeight) {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(dstWidth, dstHeight);
    uint srcX = min((uint)(uv.x * srcWidth), srcWidth - 1);
    uint srcY = min((uint)(uv.y * srcHeight), srcHeight - 1);

    float4 bgra = inputTexture.Load(int3(srcX, srcY, 0));

    uint planeOffset = dstWidth * dstHeight;
    uint index = dispatchThreadId.y * dstWidth + dispatchThreadId.x;

    outputTensor.Store(index * 4, asuint(bgra.z));
    outputTensor.Store((planeOffset + index) * 4, asuint(bgra.y));
    outputTensor.Store((planeOffset * 2 + index) * 4, asuint(bgra.x));
}
)";

std::expected<winrt::com_ptr<ID3DBlob>, std::error_code> compilePreprocessShader() {
    winrt::com_ptr<ID3DBlob> byteCode;
    winrt::com_ptr<ID3DBlob> errorBlob;

    constexpr UINT kCompileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr =
        D3DCompile(kPreprocessShaderSource.data(), kPreprocessShaderSource.size(), nullptr, nullptr,
                   nullptr, "main", "cs_5_0", kCompileFlags, 0, byteCode.put(), errorBlob.put());
    if (FAILED(hr)) {
        if (errorBlob != nullptr) {
            VF_ERROR("Preprocess shader compilation failed: {}",
                     static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    return byteCode;
}

struct InteropUpdateResult {
    std::uint64_t generationId = 0;
    bool reinitialized = false;
    bool sourceChanged = false;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* commandQueue = nullptr;
    IDMLDevice* dmlDevice = nullptr;
    ID3D12Resource* sharedInputTexture = nullptr;
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    std::uint32_t sourceFormat = 0;
};

class D3dInteropBridge {
  public:
    std::expected<InteropUpdateResult, std::error_code>
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
        VF_DEBUG("D3dInteropBridge initialized for {}x{} format={}", sourceDesc.Width,
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

        const auto signalResult = toCaptureError(
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
            toCaptureError(dx_utils::checkD3d(d3d12Queue->Wait(interopFenceD3d12.get(), fenceValue),
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

    [[nodiscard]] InteropUpdateResult buildUpdateResult(bool reinitializedState,
                                                        bool sourceChangedState) const {
        return InteropUpdateResult{
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
            toCaptureError(dx_utils::checkD3d(d3d11Device->QueryInterface(__uuidof(ID3D11Device5),
                                                                          d3d11Device5.put_void()),
                                              "ID3D11Device::QueryInterface(ID3D11Device5)"),
                           CaptureError::InferenceInitializationFailed);
        if (!queryDevice5Result) {
            return std::unexpected(queryDevice5Result.error());
        }

        const auto queryContext4Result = toCaptureError(
            dx_utils::checkD3d(d3d11Context->QueryInterface(__uuidof(ID3D11DeviceContext4),
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
            toCaptureError(dx_utils::checkD3d(d3d11Device->QueryInterface(__uuidof(IDXGIDevice),
                                                                          dxgiDevice.put_void()),
                                              "ID3D11Device::QueryInterface(IDXGIDevice)"),
                           CaptureError::InferenceInitializationFailed);
        if (!queryDxgiDeviceResult) {
            return std::unexpected(queryDxgiDeviceResult.error());
        }

        winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
        const auto getAdapterResult =
            toCaptureError(dx_utils::checkD3d(dxgiDevice->GetAdapter(dxgiAdapter.put()),
                                              "IDXGIDevice::GetAdapter"),
                           CaptureError::InferenceInitializationFailed);
        if (!getAdapterResult) {
            return std::unexpected(getAdapterResult.error());
        }

        const auto createD3d12DeviceResult = toCaptureError(
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
        const auto createQueueResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->CreateCommandQueue(
                                   &queueDesc, __uuidof(ID3D12CommandQueue), d3d12Queue.put_void()),
                               "ID3D12Device::CreateCommandQueue"),
            CaptureError::InferenceInitializationFailed);
        if (!createQueueResult) {
            return std::unexpected(createQueueResult.error());
        }

        directmlModule = LoadLibraryW(L"DirectML.dll");
        const auto loadModuleResult = toCaptureError(
            dx_utils::checkWin32(directmlModule != nullptr, "LoadLibraryW(DirectML.dll)"),
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

        const auto createDmlDeviceResult = toCaptureError(
            dx_utils::checkD3d(createDevice1(d3d12Device.get(), DML_CREATE_DEVICE_FLAG_NONE,
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

        const auto createSharedTextureResult = toCaptureError(
            dx_utils::checkD3d(d3d11Device->CreateTexture2D(&sharedInputDesc, nullptr,
                                                            sharedInputTextureD3d11.put()),
                               "ID3D11Device::CreateTexture2D(sharedInputTexture)"),
            CaptureError::InferenceInitializationFailed);
        if (!createSharedTextureResult) {
            return std::unexpected(createSharedTextureResult.error());
        }

        winrt::com_ptr<IDXGIResource1> dxgiSharedResource;
        const auto querySharedResourceResult = toCaptureError(
            dx_utils::checkD3d(sharedInputTextureD3d11->QueryInterface(
                                   __uuidof(IDXGIResource1), dxgiSharedResource.put_void()),
                               "ID3D11Texture2D::QueryInterface(IDXGIResource1)"),
            CaptureError::InferenceInitializationFailed);
        if (!querySharedResourceResult) {
            return std::unexpected(querySharedResourceResult.error());
        }

        const auto createSharedTextureHandleResult = toCaptureError(
            dx_utils::checkD3d(dxgiSharedResource->CreateSharedHandle(
                                   nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                   nullptr, sharedTextureHandle.put()),
                               "IDXGIResource1::CreateSharedHandle(inputTexture)"),
            CaptureError::InferenceInitializationFailed);
        if (!createSharedTextureHandleResult) {
            return std::unexpected(createSharedTextureHandleResult.error());
        }

        const auto openSharedTextureResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->OpenSharedHandle(sharedTextureHandle.get(),
                                                             __uuidof(ID3D12Resource),
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
            toCaptureError(dx_utils::checkD3d(d3d12Device->CreateFence(
                                                  0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence),
                                                  interopFenceD3d12.put_void()),
                                              "ID3D12Device::CreateFence(sharedInterop)"),
                           CaptureError::InferenceInitializationFailed);
        if (!createInteropFenceResult) {
            return std::unexpected(createInteropFenceResult.error());
        }

        const auto createInteropFenceHandleResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->CreateSharedHandle(interopFenceD3d12.get(), nullptr,
                                                               GENERIC_ALL, nullptr,
                                                               interopFenceHandle.put()),
                               "ID3D12Device::CreateSharedHandle(sharedInteropFence)"),
            CaptureError::InferenceInitializationFailed);
        if (!createInteropFenceHandleResult) {
            return std::unexpected(createInteropFenceHandleResult.error());
        }

        const auto openInteropFenceResult =
            toCaptureError(dx_utils::checkD3d(d3d11Device5->OpenSharedFence(
                                                  interopFenceHandle.get(), __uuidof(ID3D11Fence),
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

class PreprocessComputePipeline {
  public:
    struct InitConfig {
        std::uint32_t dstWidth = 0;
        std::uint32_t dstHeight = 0;
        std::size_t outputBytes = 0;
        std::size_t outputElementCount = 0;
    };

    std::expected<void, std::error_code> initialize(ID3D12Device* device,
                                                    const InitConfig& config) {
        if (device == nullptr || config.outputBytes == 0 || config.outputElementCount == 0 ||
            config.dstWidth == 0 || config.dstHeight == 0) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        if (initialized) {
            const bool sameDevice = d3d12Device.get() == device;
            if (dstWidth == config.dstWidth && dstHeight == config.dstHeight &&
                outputBytesValue == config.outputBytes && sameDevice &&
                outputElementCount == config.outputElementCount) {
                return {};
            }
            reset();
        }

        bool commit = false;
        auto rollback = dx_utils::makeScopeExit([this, &commit]() {
            if (!commit) {
                reset();
            }
        });

        d3d12Device.copy_from(device);
        dstWidth = config.dstWidth;
        dstHeight = config.dstHeight;
        outputBytesValue = config.outputBytes;
        outputElementCount = config.outputElementCount;

        const auto shaderBlobResult = compilePreprocessShader();
        if (!shaderBlobResult) {
            return std::unexpected(shaderBlobResult.error());
        }
        const winrt::com_ptr<ID3DBlob>& shaderBlob = shaderBlobResult.value();

        const D3D12_HEAP_PROPERTIES heapProperties = dx_utils::makeDefaultHeapProperties();
        const D3D12_RESOURCE_DESC outputDesc = dx_utils::makeRawBufferDesc(
            static_cast<UINT64>(outputBytesValue), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        const auto createOutputResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->CreateCommittedResource(
                                   &heapProperties, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                   D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
                                   outputResource.put_void()),
                               "ID3D12Device::CreateCommittedResource(output)"),
            CaptureError::InferenceInitializationFailed);
        if (!createOutputResult) {
            return std::unexpected(createOutputResult.error());
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 2> descriptorRanges{};
        descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRanges[0].NumDescriptors = 1;
        descriptorRanges[0].BaseShaderRegister = 0;
        descriptorRanges[0].RegisterSpace = 0;
        descriptorRanges[0].OffsetInDescriptorsFromTableStart = 0;

        descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorRanges[1].NumDescriptors = 1;
        descriptorRanges[1].BaseShaderRegister = 0;
        descriptorRanges[1].RegisterSpace = 0;
        descriptorRanges[1].OffsetInDescriptorsFromTableStart = 0;

        std::array<D3D12_ROOT_PARAMETER, 3> rootParameters{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.RegisterSpace = 0;
        rootParameters[0].Constants.Num32BitValues = 4;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRanges.data();
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRanges.data() + 1;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters = static_cast<UINT>(rootParameters.size());
        rootSigDesc.pParameters = rootParameters.data();
        rootSigDesc.NumStaticSamplers = 0;
        rootSigDesc.pStaticSamplers = nullptr;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        winrt::com_ptr<ID3DBlob> rootSigBlob;
        winrt::com_ptr<ID3DBlob> rootSigErr;
        const auto serializeResult =
            toCaptureError(dx_utils::checkD3d(D3D12SerializeRootSignature(
                                                  &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                  rootSigBlob.put(), rootSigErr.put()),
                                              "D3D12SerializeRootSignature"),
                           CaptureError::InferenceInitializationFailed);
        if (!serializeResult) {
            if (rootSigErr != nullptr) {
                VF_ERROR("D3D12 root signature serialization message: {}",
                         static_cast<const char*>(rootSigErr->GetBufferPointer()));
            }
            return std::unexpected(serializeResult.error());
        }

        const auto createRootResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->CreateRootSignature(
                                   0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                   __uuidof(ID3D12RootSignature), rootSignature.put_void()),
                               "ID3D12Device::CreateRootSignature"),
            CaptureError::InferenceInitializationFailed);
        if (!createRootResult) {
            return std::unexpected(createRootResult.error());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature.get();
        psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        const auto createPsoResult =
            toCaptureError(dx_utils::checkD3d(d3d12Device->CreateComputePipelineState(
                                                  &psoDesc, __uuidof(ID3D12PipelineState),
                                                  pipelineState.put_void()),
                                              "ID3D12Device::CreateComputePipelineState"),
                           CaptureError::InferenceInitializationFailed);
        if (!createPsoResult) {
            return std::unexpected(createPsoResult.error());
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 2;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NodeMask = 0;

        const auto createHeapResult =
            toCaptureError(dx_utils::checkD3d(d3d12Device->CreateDescriptorHeap(
                                                  &heapDesc, __uuidof(ID3D12DescriptorHeap),
                                                  descriptorHeap.put_void()),
                                              "ID3D12Device::CreateDescriptorHeap"),
                           CaptureError::InferenceInitializationFailed);
        if (!createHeapResult) {
            return std::unexpected(createHeapResult.error());
        }

        descriptorIncrement =
            d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        uavCpu.ptr += descriptorIncrement;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = static_cast<UINT>(outputElementCount);
        uavDesc.Buffer.StructureByteStride = 0;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        d3d12Device->CreateUnorderedAccessView(outputResource.get(), nullptr, &uavDesc, uavCpu);

        const auto createAllocatorResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                   __uuidof(ID3D12CommandAllocator),
                                                                   commandAllocator.put_void()),
                               "ID3D12Device::CreateCommandAllocator"),
            CaptureError::InferenceInitializationFailed);
        if (!createAllocatorResult) {
            return std::unexpected(createAllocatorResult.error());
        }

        const auto createListResult = toCaptureError(
            dx_utils::checkD3d(d3d12Device->CreateCommandList(
                                   0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(),
                                   pipelineState.get(), __uuidof(ID3D12GraphicsCommandList),
                                   commandList.put_void()),
                               "ID3D12Device::CreateCommandList"),
            CaptureError::InferenceInitializationFailed);
        if (!createListResult) {
            return std::unexpected(createListResult.error());
        }

        const auto closeInitListResult = toCaptureError(
            dx_utils::checkD3d(commandList->Close(), "ID3D12GraphicsCommandList::Close(initial)"),
            CaptureError::InferenceInitializationFailed);
        if (!closeInitListResult) {
            return std::unexpected(closeInitListResult.error());
        }

        const auto createFenceResult =
            toCaptureError(dx_utils::checkD3d(d3d12Device->CreateFence(
                                                  0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                                  completionFenceRef.put_void()),
                                              "ID3D12Device::CreateFence"),
                           CaptureError::InferenceInitializationFailed);
        if (!createFenceResult) {
            return std::unexpected(createFenceResult.error());
        }

        completionEventRef.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        const auto createEventResult =
            toCaptureError(dx_utils::checkWin32(completionEventRef.get() != nullptr,
                                                "CreateEventW(computeCompletionEvent)"),
                           CaptureError::InferenceInitializationFailed);
        if (!createEventResult) {
            return std::unexpected(createEventResult.error());
        }

        fenceValue = 0;
        initialized = true;
        commit = true;
        rollback.dismiss();
        return {};
    }

    std::expected<void, std::error_code> updateResources(ID3D12Resource* inputTexture,
                                                         DXGI_FORMAT inputFormat) {
        if (!initialized || d3d12Device == nullptr || descriptorHeap == nullptr ||
            outputResource == nullptr || inputTexture == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        inputResource = inputTexture;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = inputFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0F;
        d3d12Device->CreateShaderResourceView(inputTexture, &srvDesc, srvCpu);

        return {};
    }

    std::expected<void, std::error_code> prepareForRecord() const {
        if (!initialized || commandAllocator == nullptr || commandList == nullptr ||
            pipelineState == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto resetAllocatorResult = toCaptureError(
            dx_utils::checkD3d(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset"),
            CaptureError::InferenceRunFailed);
        if (!resetAllocatorResult) {
            return std::unexpected(resetAllocatorResult.error());
        }

        const auto resetListResult = toCaptureError(
            dx_utils::checkD3d(commandList->Reset(commandAllocator.get(), pipelineState.get()),
                               "ID3D12GraphicsCommandList::Reset"),
            CaptureError::InferenceRunFailed);
        if (!resetListResult) {
            return std::unexpected(resetListResult.error());
        }

        return {};
    }

    std::expected<void, std::error_code> recordDispatch(std::uint32_t srcWidth,
                                                        std::uint32_t srcHeight) {
        if (!initialized || commandList == nullptr || rootSignature == nullptr ||
            descriptorHeap == nullptr || inputResource == nullptr || outputResource == nullptr ||
            dstWidth == 0 || dstHeight == 0) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const std::array<std::uint32_t, 4> constants = {srcWidth, srcHeight, dstWidth, dstHeight};
        std::array<ID3D12DescriptorHeap*, 1> heaps = {descriptorHeap.get()};
        commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
        commandList->SetComputeRootSignature(rootSignature.get());
        commandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(constants.size()),
                                                  constants.data(), 0);

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = srvGpu;
        uavGpu.ptr += descriptorIncrement;
        commandList->SetComputeRootDescriptorTable(1, srvGpu);
        commandList->SetComputeRootDescriptorTable(2, uavGpu);

        dx_utils::transitionResource(commandList.get(), inputResource, D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dx_utils::transitionResource(commandList.get(), outputResource.get(),
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        const UINT dispatchX = (dstWidth + 7U) / 8U;
        const UINT dispatchY = (dstHeight + 7U) / 8U;
        commandList->Dispatch(dispatchX, dispatchY, 1U);

        dx_utils::transitionResource(commandList.get(), inputResource,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                     D3D12_RESOURCE_STATE_COMMON);
        dx_utils::transitionResource(commandList.get(), outputResource.get(),
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_COMMON);
        return {};
    }

    std::expected<void, std::error_code> closeAfterRecord() const {
        if (!initialized || commandList == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto closeResult = toCaptureError(
            dx_utils::checkD3d(commandList->Close(), "ID3D12GraphicsCommandList::Close"),
            CaptureError::InferenceRunFailed);
        if (!closeResult) {
            return std::unexpected(closeResult.error());
        }
        return {};
    }

    [[nodiscard]] ID3D12CommandList* commandListForExecute() const { return commandList.get(); }
    [[nodiscard]] ID3D12Fence* getCompletionFence() const { return completionFenceRef.get(); }
    [[nodiscard]] HANDLE getCompletionEvent() const { return completionEventRef.get(); }

    [[nodiscard]] std::uint64_t nextFenceValue() {
        ++fenceValue;
        return fenceValue;
    }

    [[nodiscard]] ID3D12Resource* getOutputResource() const { return outputResource.get(); }
    [[nodiscard]] std::size_t getOutputBytes() const { return outputBytesValue; }

    void reset() {
        completionFenceRef = nullptr;
        commandList = nullptr;
        commandAllocator = nullptr;
        descriptorHeap = nullptr;
        pipelineState = nullptr;
        rootSignature = nullptr;
        outputResource = nullptr;
        d3d12Device = nullptr;
        inputResource = nullptr;
        descriptorIncrement = 0;
        outputBytesValue = 0;
        outputElementCount = 0;
        dstWidth = 0;
        dstHeight = 0;
        fenceValue = 0;
        initialized = false;

        completionEventRef.reset();
    }

  private:
    bool initialized = false;

    winrt::com_ptr<ID3D12Device> d3d12Device;
    winrt::com_ptr<ID3D12Resource> outputResource;
    winrt::com_ptr<ID3D12RootSignature> rootSignature;
    winrt::com_ptr<ID3D12PipelineState> pipelineState;
    winrt::com_ptr<ID3D12DescriptorHeap> descriptorHeap;
    winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
    winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
    winrt::com_ptr<ID3D12Fence> completionFenceRef;

    ID3D12Resource* inputResource = nullptr;
    dx_utils::UniqueWin32Handle completionEventRef;
    UINT descriptorIncrement = 0;
    std::size_t outputBytesValue = 0;
    std::size_t outputElementCount = 0;
    std::uint32_t dstWidth = 0;
    std::uint32_t dstHeight = 0;
    std::uint64_t fenceValue = 0;
};

} // namespace

class DmlImageProcessor::Impl {
  public:
    explicit Impl(OnnxDmlSession& session) : session(session) {}

    std::expected<InitializeResult, std::error_code> initialize(ID3D11Texture2D* sourceTexture) {
        if (sourceTexture == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        std::scoped_lock lock(mutex);

        const auto interopResult = interop.initializeOrUpdate(sourceTexture);
        if (!interopResult) {
            return std::unexpected(interopResult.error());
        }
        const InteropUpdateResult& interopState = interopResult.value();

        const auto setupResult = prepareInferencePath(interopState);
        if (!setupResult) {
            return std::unexpected(setupResult.error());
        }

        initialized = true;
        return InitializeResult{
            .dmlDevice = interopState.dmlDevice,
            .commandQueue = interopState.commandQueue,
        };
    }

    std::expected<DispatchResult, std::error_code> dispatch(ID3D11Texture2D* frameTexture,
                                                            std::uint64_t fenceValue) {
        if (frameTexture == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
        }

        std::scoped_lock lock(mutex);
        if (!initialized) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto interopInitResult = interop.initializeOrUpdate(frameTexture);
        if (!interopInitResult) {
            return std::unexpected(interopInitResult.error());
        }
        const InteropUpdateResult& interopState = interopInitResult.value();

        const auto setupResult = prepareInferencePath(interopState);
        if (!setupResult) {
            return std::unexpected(setupResult.error());
        }

        const auto copyResult = interop.copyAndSignal(frameTexture, fenceValue);
        if (!copyResult) {
            return std::unexpected(copyResult.error());
        }

        const auto waitResult = interop.waitOnQueue(copyResult.value());
        if (!waitResult) {
            return std::unexpected(waitResult.error());
        }

        const auto prepareResult = preprocessPipeline.prepareForRecord();
        if (!prepareResult) {
            return std::unexpected(prepareResult.error());
        }

        const auto recordResult =
            preprocessPipeline.recordDispatch(interopState.sourceWidth, interopState.sourceHeight);
        if (!recordResult) {
            return std::unexpected(recordResult.error());
        }

        const auto closeResult = preprocessPipeline.closeAfterRecord();
        if (!closeResult) {
            return std::unexpected(closeResult.error());
        }

        ID3D12CommandQueue* queue = interopState.commandQueue;
        ID3D12CommandList* commandList = preprocessPipeline.commandListForExecute();
        ID3D12Fence* completionFence = preprocessPipeline.getCompletionFence();
        HANDLE completionEvent = preprocessPipeline.getCompletionEvent();

        if (queue == nullptr || commandList == nullptr || completionFence == nullptr ||
            completionEvent == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        std::array<ID3D12CommandList*, 1> commandLists = {commandList};
        queue->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());

        const std::uint64_t completionFenceValue = preprocessPipeline.nextFenceValue();
        const auto signalResult =
            toCaptureError(dx_utils::checkD3d(queue->Signal(completionFence, completionFenceValue),
                                              "ID3D12CommandQueue::Signal(preprocess)"),
                           CaptureError::InferenceRunFailed);
        if (!signalResult) {
            return std::unexpected(signalResult.error());
        }

        if (completionFence->GetCompletedValue() < completionFenceValue) {
            const auto setEventResult =
                toCaptureError(dx_utils::checkD3d(completionFence->SetEventOnCompletion(
                                                      completionFenceValue, completionEvent),
                                                  "ID3D12Fence::SetEventOnCompletion(preprocess)"),
                               CaptureError::InferenceRunFailed);
            if (!setEventResult) {
                return std::unexpected(setEventResult.error());
            }

            const DWORD waitCode = WaitForSingleObject(completionEvent, INFINITE);
            const auto waitEventResult =
                toCaptureError(dx_utils::checkWin32(waitCode == WAIT_OBJECT_0,
                                                    "WaitForSingleObject(preprocessFenceEvent)"),
                               CaptureError::InferenceRunFailed);
            if (!waitEventResult) {
                return std::unexpected(waitEventResult.error());
            }
        }

        return DispatchResult{
            .outputResource = preprocessPipeline.getOutputResource(),
            .outputBytes = preprocessPipeline.getOutputBytes(),
        };
    }

    void shutdown() {
        std::scoped_lock lock(mutex);
        preprocessPipeline.reset();
        interop.reset();
        initialized = false;
    }

  private:
    std::expected<void, std::error_code>
    prepareInferencePath(const InteropUpdateResult& interopState) {
        const auto sessionStartResult = session.start(
            interopState.dmlDevice, interopState.commandQueue, interopState.generationId);
        if (!sessionStartResult) {
            return std::unexpected(sessionStartResult.error());
        }

        const OnnxDmlSession::ModelMetadata& metadata = session.metadata();
        PreprocessComputePipeline::InitConfig initConfig{};
        initConfig.dstWidth = metadata.inputWidth;
        initConfig.dstHeight = metadata.inputHeight;
        initConfig.outputBytes = metadata.inputTensorBytes;
        initConfig.outputElementCount = metadata.inputElementCount;

        const auto pipelineInitResult =
            preprocessPipeline.initialize(interopState.device, initConfig);
        if (!pipelineInitResult) {
            return std::unexpected(pipelineInitResult.error());
        }

        const auto pipelineUpdateResult = preprocessPipeline.updateResources(
            interopState.sharedInputTexture, static_cast<DXGI_FORMAT>(interopState.sourceFormat));
        if (!pipelineUpdateResult) {
            return std::unexpected(pipelineUpdateResult.error());
        }

        return {};
    }

    OnnxDmlSession& session;
    std::mutex mutex;
    bool initialized = false;
    D3dInteropBridge interop;
    PreprocessComputePipeline preprocessPipeline;
};

DmlImageProcessor::DmlImageProcessor(OnnxDmlSession& session)
    : impl(std::make_unique<Impl>(session)) {}

DmlImageProcessor::~DmlImageProcessor() { impl->shutdown(); }

std::expected<DmlImageProcessor::InitializeResult, std::error_code>
DmlImageProcessor::initialize(ID3D11Texture2D* sourceTexture) {
    return impl->initialize(sourceTexture);
}

std::expected<DmlImageProcessor::DispatchResult, std::error_code>
DmlImageProcessor::dispatch(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue) {
    return impl->dispatch(frameTexture, fenceValue);
}

void DmlImageProcessor::shutdown() { impl->shutdown(); }

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

#else

class DmlImageProcessor::Impl {
  public:
    explicit Impl(OnnxDmlSession& session) { static_cast<void>(session); }
};

DmlImageProcessor::DmlImageProcessor(OnnxDmlSession& session)
    : impl(std::make_unique<Impl>(session)) {}

DmlImageProcessor::~DmlImageProcessor() = default;

std::expected<DmlImageProcessor::InitializeResult, std::error_code>
DmlImageProcessor::initialize(void* sourceTexture) {
    static_cast<void>(sourceTexture);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<DmlImageProcessor::DispatchResult, std::error_code>
DmlImageProcessor::dispatch(void* frameTexture, std::uint64_t fenceValue) {
    static_cast<void>(frameTexture);
    static_cast<void>(fenceValue);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

void DmlImageProcessor::shutdown() {}

#endif

} // namespace vf
