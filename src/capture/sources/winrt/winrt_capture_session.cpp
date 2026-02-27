#include "capture/sources/winrt/winrt_capture_session.hpp"

#include <expected>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <vector>

#include <Windows.Graphics.Capture.Interop.h>
#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#endif

namespace vf {

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
WinrtCaptureSession::initializeDeviceAndItem(std::uint32_t preferredDisplayIndex) {
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

std::expected<void, std::error_code>
WinrtCaptureSession::initializeFramePool(const FrameArrivedHandler& frameArrivedHandler) {
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

        frameArrivedToken =
            framePool.FrameArrived([frameArrivedHandler](const auto& sender, const auto& args) {
                frameArrivedHandler(sender, args);
            });

        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(CaptureError::FramePoolInitializationFailed));
    }
}

std::expected<void, std::error_code> WinrtCaptureSession::startSession() {
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

std::expected<void, std::error_code> WinrtCaptureSession::stop() {
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePoolSnapshot{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession captureSessionSnapshot{nullptr};
    winrt::event_token token{};

    framePoolSnapshot = framePool;
    captureSessionSnapshot = captureSession;
    token = std::exchange(frameArrivedToken, winrt::event_token{});

    framePool = nullptr;
    captureSession = nullptr;
    captureItem = nullptr;
    direct3dDevice = nullptr;
    d3dContext = nullptr;
    d3dDevice = nullptr;

    std::error_code stopError;
    if (framePoolSnapshot && token.value != 0) {
        try {
            framePoolSnapshot.FrameArrived(token);
        } catch (const winrt::hresult_error&) {
            if (!stopError) {
                stopError = makeErrorCode(CaptureError::SessionStopFailed);
            }
        }
    }

    if (captureSessionSnapshot) {
        try {
            captureSessionSnapshot.Close();
        } catch (const winrt::hresult_error&) {
            if (!stopError) {
                stopError = makeErrorCode(CaptureError::SessionStopFailed);
            }
        }
    }

    if (framePoolSnapshot) {
        try {
            framePoolSnapshot.Close();
        } catch (const winrt::hresult_error&) {
            if (!stopError) {
                stopError = makeErrorCode(CaptureError::SessionStopFailed);
            }
        }
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    return {};
}

#else

std::expected<void, std::error_code> WinrtCaptureSession::stop() {
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

#endif

} // namespace vf
