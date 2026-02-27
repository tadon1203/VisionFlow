#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"

#ifdef _WIN32
#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#endif

namespace vf {

class WinrtCaptureSession {
  public:
#ifdef _WIN32
    using FrameArrivedHandler = std::function<void(
        const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
        const winrt::Windows::Foundation::IInspectable& args)>;
#endif

    WinrtCaptureSession() = default;
    WinrtCaptureSession(const WinrtCaptureSession&) = delete;
    WinrtCaptureSession(WinrtCaptureSession&&) = delete;
    WinrtCaptureSession& operator=(const WinrtCaptureSession&) = delete;
    WinrtCaptureSession& operator=(WinrtCaptureSession&&) = delete;
    ~WinrtCaptureSession() = default;

#ifdef _WIN32
    [[nodiscard]] std::expected<void, std::error_code>
    initializeDeviceAndItem(std::uint32_t preferredDisplayIndex);
    [[nodiscard]] std::expected<void, std::error_code>
    initializeFramePool(const FrameArrivedHandler& frameArrivedHandler);
    [[nodiscard]] std::expected<void, std::error_code> startSession();
#endif
    [[nodiscard]] std::expected<void, std::error_code> stop();

  private:
#ifdef _WIN32
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice direct3dDevice{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem captureItem{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession captureSession{nullptr};
    winrt::event_token frameArrivedToken{};
#endif
};

} // namespace vf
