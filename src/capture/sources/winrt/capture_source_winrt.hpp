#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/core/i_profiler.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

#ifdef _WIN32
#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#endif

namespace vf {

class WinrtCaptureSession;

class WinrtCaptureSource {
  public:
    explicit WinrtCaptureSource(IWinrtFrameSink& frameSink, IProfiler* profiler = nullptr);
    WinrtCaptureSource(const WinrtCaptureSource&) = delete;
    WinrtCaptureSource(WinrtCaptureSource&&) = delete;
    WinrtCaptureSource& operator=(const WinrtCaptureSource&) = delete;
    WinrtCaptureSource& operator=(WinrtCaptureSource&&) = delete;
    ~WinrtCaptureSource() noexcept;

    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config);
    [[nodiscard]] std::expected<void, std::error_code> stop();
    [[nodiscard]] std::expected<void, std::error_code> poll();

  private:
    enum class CaptureState : std::uint8_t {
        Idle,
        Starting,
        Running,
        Stopping,
        Fault,
    };

#ifdef _WIN32
    struct ArrivedFrame {
        winrt::com_ptr<ID3D11Texture2D> texture;
        CaptureFrameInfo info;
    };

    [[nodiscard]] bool isRunning();
    [[nodiscard]] static std::optional<ArrivedFrame> tryAcquireArrivedFrame(
        const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender);
    static void forwardFrameToSink(IWinrtFrameSink& sink, const ArrivedFrame& frame);
    void markFault(const std::error_code& error);

    void onFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
                        const winrt::Windows::Foundation::IInspectable& args);
#endif

    std::mutex stateMutex;
    CaptureState state = CaptureState::Idle;
    IWinrtFrameSink& frameSink;
    std::error_code lastError;
    IProfiler* profiler = nullptr;

#ifdef _WIN32
    std::unique_ptr<WinrtCaptureSession> session;
#endif
};

} // namespace vf
