#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <system_error>

#include "VisionFlow/core/config.hpp"
#include "capture/winrt/i_winrt_frame_sink.hpp"

#ifdef _WIN32
#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#endif

namespace vf {

class WinrtCaptureSource {
  public:
    WinrtCaptureSource();
    WinrtCaptureSource(const WinrtCaptureSource&) = delete;
    WinrtCaptureSource(WinrtCaptureSource&&) = delete;
    WinrtCaptureSource& operator=(const WinrtCaptureSource&) = delete;
    WinrtCaptureSource& operator=(WinrtCaptureSource&&) = delete;
    ~WinrtCaptureSource();

    void setFrameSink(std::shared_ptr<IWinrtFrameSink> frameSink);
    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config);
    [[nodiscard]] std::expected<void, std::error_code> stop();

  private:
    enum class CaptureState : std::uint8_t {
        Idle,
        Starting,
        Running,
        Stopping,
        Fault,
    };

#ifdef _WIN32
    [[nodiscard]] std::expected<void, std::error_code>
    initializeDeviceAndItem(std::uint32_t preferredDisplayIndex);
    [[nodiscard]] std::expected<void, std::error_code> initializeFramePool();
    [[nodiscard]] std::expected<void, std::error_code> startSession();
    void onFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
                        const winrt::Windows::Foundation::IInspectable& args);
#endif

    std::mutex stateMutex;
    CaptureState state = CaptureState::Idle;
    std::shared_ptr<IWinrtFrameSink> frameSink;

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
