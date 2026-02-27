#include "capture/winrt/winrt_capture_source.hpp"

#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/winrt/i_winrt_frame_sink.hpp"

#ifdef _WIN32
#include <vector>

#include <Windows.Graphics.Capture.Interop.h>
#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>
#endif

namespace vf {

WinrtCaptureSource::WinrtCaptureSource() = default;

WinrtCaptureSource::~WinrtCaptureSource() {
    const std::expected<void, std::error_code> result = stop();
    if (!result) {
        VF_WARN("WinrtCaptureSource stop during destruction failed: {}", result.error().message());
    }
}

void WinrtCaptureSource::setFrameSink(std::shared_ptr<IWinrtFrameSink> nextFrameSink) {
    std::scoped_lock lock(stateMutex);
    frameSink = std::move(nextFrameSink);
}

std::expected<void, std::error_code> WinrtCaptureSource::start(const CaptureConfig& config) {
#ifndef _WIN32
    static_cast<void>(config);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(stateMutex);
        if (state == CaptureState::Running) {
            return {};
        }
        if (state == CaptureState::Starting || state == CaptureState::Stopping) {
            return std::unexpected(makeErrorCode(CaptureError::InvalidState));
        }
        state = CaptureState::Starting;
    }

    auto markFault = [this]() {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Fault;
    };

    try {
        const std::expected<void, std::error_code> initializeResult =
            initializeDeviceAndItem(config.preferredDisplayIndex);
        if (!initializeResult) {
            markFault();
            VF_ERROR("WinrtCaptureSource start failed during device/item initialization: {}",
                     initializeResult.error().message());
            return std::unexpected(initializeResult.error());
        }
    } catch (const winrt::hresult_error& ex) {
        markFault();
        VF_ERROR("WinrtCaptureSource start failed during device/item initialization: {}",
                 winrt::to_string(ex.message()));
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }

    try {
        const std::expected<void, std::error_code> framePoolResult = initializeFramePool();
        if (!framePoolResult) {
            markFault();
            VF_ERROR("WinrtCaptureSource start failed during frame pool initialization: {}",
                     framePoolResult.error().message());
            return std::unexpected(framePoolResult.error());
        }
    } catch (const winrt::hresult_error& ex) {
        markFault();
        VF_ERROR("WinrtCaptureSource start failed during frame pool initialization: {}",
                 winrt::to_string(ex.message()));
        return std::unexpected(makeErrorCode(CaptureError::FramePoolInitializationFailed));
    }

    try {
        const std::expected<void, std::error_code> sessionResult = startSession();
        if (!sessionResult) {
            markFault();
            VF_ERROR("WinrtCaptureSource start failed during session start: {}",
                     sessionResult.error().message());
            return std::unexpected(sessionResult.error());
        }
    } catch (const winrt::hresult_error& ex) {
        markFault();
        VF_ERROR("WinrtCaptureSource start failed during session start: {}",
                 winrt::to_string(ex.message()));
        return std::unexpected(makeErrorCode(CaptureError::SessionStartFailed));
    }

    {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Running;
    }

    VF_INFO("WinrtCaptureSource started (display index: {})", config.preferredDisplayIndex);
    return {};
#endif
}

std::expected<void, std::error_code> WinrtCaptureSource::stop() {
#ifndef _WIN32
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePoolSnapshot{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession captureSessionSnapshot{nullptr};
    winrt::event_token token{};

    {
        std::scoped_lock lock(stateMutex);
        if (state == CaptureState::Idle) {
            return {};
        }
        state = CaptureState::Stopping;

        framePoolSnapshot = framePool;
        captureSessionSnapshot = captureSession;
        token = std::exchange(frameArrivedToken, winrt::event_token{});

        framePool = nullptr;
        captureSession = nullptr;
        captureItem = nullptr;
        direct3dDevice = nullptr;
        d3dContext = nullptr;
        d3dDevice = nullptr;
    }

    std::error_code stopError;
    if (framePoolSnapshot && token.value != 0) {
        try {
            framePoolSnapshot.FrameArrived(token);
        } catch (const winrt::hresult_error& ex) {
            VF_WARN("WinrtCaptureSource stop failed while unsubscribing frame handler: {}",
                    winrt::to_string(ex.message()));
            if (!stopError) {
                stopError = makeErrorCode(CaptureError::SessionStopFailed);
            }
        }
    }

    if (captureSessionSnapshot) {
        try {
            captureSessionSnapshot.Close();
        } catch (const winrt::hresult_error& ex) {
            VF_WARN("WinrtCaptureSource stop failed while closing capture session: {}",
                    winrt::to_string(ex.message()));
            if (!stopError) {
                stopError = makeErrorCode(CaptureError::SessionStopFailed);
            }
        }
    }

    if (framePoolSnapshot) {
        try {
            framePoolSnapshot.Close();
        } catch (const winrt::hresult_error& ex) {
            VF_WARN("WinrtCaptureSource stop failed while closing frame pool: {}",
                    winrt::to_string(ex.message()));
            if (!stopError) {
                stopError = makeErrorCode(CaptureError::SessionStopFailed);
            }
        }
    }

    {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Idle;
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    VF_INFO("WinrtCaptureSource stopped");
    return {};
#endif
}

#ifdef _WIN32

namespace {

[[nodiscard]] std::expected<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, std::error_code>
createCaptureItemForDisplay(std::uint32_t preferredDisplayIndex) {
    std::vector<HMONITOR> monitors;
    const auto enumProc = [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
        auto* out =
            reinterpret_cast<std::vector<HMONITOR>*>(context); // NOLINT(performance-no-int-to-ptr)
        out->push_back(monitor);
        return TRUE;
    };

    const BOOL enumResult =
        EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&monitors));
    if (enumResult == FALSE || monitors.empty()) {
        return std::unexpected(makeErrorCode(CaptureError::DisplayNotFound));
    }

    const std::uint32_t selectedIndex =
        preferredDisplayIndex < monitors.size() ? preferredDisplayIndex : 0U;

    try {
        auto interopFactory =
            winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                          IGraphicsCaptureItemInterop>();

        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
        const HRESULT hr = interopFactory->CreateForMonitor(
            monitors[selectedIndex],
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            winrt::put_abi(item));
        if (FAILED(hr)) {
            return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
        }

        return item;
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }
}

[[nodiscard]] std::expected<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice,
                            std::error_code>
createDirect3dDevice(const winrt::com_ptr<ID3D11Device>& d3dDevice) {
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
    if (FAILED(hr)) {
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }

    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put());
    if (FAILED(hr)) {
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }

    try {
        return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }
}

} // namespace

std::expected<void, std::error_code>
WinrtCaptureSource::initializeDeviceAndItem(std::uint32_t preferredDisplayIndex) {
    constexpr UINT kCreateFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};

    D3D_FEATURE_LEVEL createdLevel{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, kCreateFlags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, d3dDevice.put(),
                                   &createdLevel, d3dContext.put());
    if (FAILED(hr)) {
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }

    const auto direct3dResult = createDirect3dDevice(d3dDevice);
    if (!direct3dResult) {
        return std::unexpected(direct3dResult.error());
    }
    direct3dDevice = direct3dResult.value();

    const auto itemResult = createCaptureItemForDisplay(preferredDisplayIndex);
    if (!itemResult) {
        return std::unexpected(itemResult.error());
    }
    captureItem = itemResult.value();

    return {};
}

std::expected<void, std::error_code> WinrtCaptureSource::initializeFramePool() {
    if (!direct3dDevice || !captureItem) {
        return std::unexpected(makeErrorCode(CaptureError::FramePoolInitializationFailed));
    }

    try {
        const auto size = captureItem.Size();
        framePool =
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                direct3dDevice,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                size);

        frameArrivedToken = framePool.FrameArrived(
            [this](const auto& sender, const auto& args) { onFrameArrived(sender, args); });

        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(CaptureError::FramePoolInitializationFailed));
    }
}

std::expected<void, std::error_code> WinrtCaptureSource::startSession() {
    if (!framePool || !captureItem) {
        return std::unexpected(makeErrorCode(CaptureError::SessionStartFailed));
    }

    try {
        captureSession = framePool.CreateCaptureSession(captureItem);
        captureSession.IsBorderRequired(false);
        captureSession.StartCapture();
        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(CaptureError::SessionStartFailed));
    }
}

void WinrtCaptureSource::onFrameArrived(
    const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
    const winrt::Windows::Foundation::IInspectable& args) {
    static_cast<void>(args);

    std::shared_ptr<IWinrtFrameSink> frameSinkSnapshot;
    {
        std::scoped_lock lock(stateMutex);
        if (state != CaptureState::Running) {
            return;
        }
        frameSinkSnapshot = frameSink;
    }
    if (!frameSinkSnapshot) {
        return;
    }

    {
        std::scoped_lock lock(stateMutex);
        if (state != CaptureState::Running) {
            return;
        }
    }

    try {
        const auto frame = sender.TryGetNextFrame();
        if (!frame) {
            return;
        }

        const auto surface = frame.Surface();
        if (!surface) {
            return;
        }

        auto access =
            surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        void* textureRaw = nullptr;
        const HRESULT hr = access->GetInterface(__uuidof(ID3D11Texture2D), &textureRaw);
        if (SUCCEEDED(hr)) {
            texture.attach(static_cast<ID3D11Texture2D*>(textureRaw));
        }
        if (FAILED(hr) || !texture) {
            return;
        }

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        CaptureFrameInfo info;
        info.width = desc.Width;
        info.height = desc.Height;
        info.systemRelativeTime100ns = frame.SystemRelativeTime().count();

        {
            std::scoped_lock lock(stateMutex);
            if (state != CaptureState::Running) {
                return;
            }
        }

        frameSinkSnapshot->onFrame(texture.get(), info);
    } catch (const winrt::hresult_error& ex) {
        VF_WARN("WinrtCaptureSource frame processing failed with WinRT exception: {}",
                winrt::to_string(ex.message()));
    } catch (const std::exception& ex) {
        VF_WARN("WinrtCaptureSource frame processing failed with exception: {}", ex.what());
    } catch (...) {
        VF_WARN("WinrtCaptureSource frame processing failed with unknown exception");
    }
}

#endif

} // namespace vf
