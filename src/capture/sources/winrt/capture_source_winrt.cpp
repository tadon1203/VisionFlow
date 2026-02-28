#include "capture/sources/winrt/capture_source_winrt.hpp"

#include <chrono>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/sources/winrt/winrt_capture_session.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"
#include "core/expected_utils.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#endif

namespace vf {

#ifdef _WIN32
namespace {

template <typename Fn, typename MarkFaultFn>
[[nodiscard]] std::expected<void, std::error_code>
runCaptureStartStep(Fn stepFn, std::string_view stepName, CaptureError exceptionError,
                    MarkFaultFn markFault) {
    try {
        const std::expected<void, std::error_code> stepResult = stepFn();
        if (!stepResult) {
            markFault(stepResult.error());
            VF_ERROR("WinrtCaptureSource start failed during {}: {}", stepName,
                     stepResult.error().message());
            return std::unexpected(stepResult.error());
        }
        return {};
    } catch (const winrt::hresult_error& ex) {
        const std::error_code error = makeErrorCode(exceptionError);
        markFault(error);
        VF_ERROR("WinrtCaptureSource start failed during {}: {}", stepName,
                 winrt::to_string(ex.message()));
        return std::unexpected(error);
    }
}

} // namespace
#endif

WinrtCaptureSource::WinrtCaptureSource(IProfiler* profiler)
#ifdef _WIN32
    : profiler(profiler), session(std::make_unique<WinrtCaptureSession>())
#else
    : profiler(profiler)
#endif
{
}

WinrtCaptureSource::~WinrtCaptureSource() noexcept {
    try {
        const std::expected<void, std::error_code> result = stop();
        if (!result) {
            VF_WARN("WinrtCaptureSource stop during destruction failed: {}",
                    result.error().message());
        }
    } catch (...) {
        VF_WARN("WinrtCaptureSource stop during destruction failed with exception");
    }
}

void WinrtCaptureSource::bindFrameSink(IWinrtFrameSink* nextFrameSink) {
    std::scoped_lock lock(stateMutex);
    frameSink = nextFrameSink;
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
        lastError.clear();
    }

    auto markFault = [this](const std::error_code& error) {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Fault;
        lastError = error;
    };

    if (session == nullptr) {
        markFault(makeErrorCode(CaptureError::InvalidState));
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    const auto initializeStepResult = runCaptureStartStep(
        [this, &config]() {
            return session->initializeDeviceAndItem(config.preferredDisplayIndex);
        },
        "device/item initialization", CaptureError::DeviceInitializationFailed, markFault);
    if (!initializeStepResult) {
        return propagateFailure(initializeStepResult);
    }

    const auto framePoolStepResult = runCaptureStartStep(
        [this]() {
            return session->initializeFramePool(
                [this](const auto& sender, const auto& args) { onFrameArrived(sender, args); });
        },
        "frame pool initialization", CaptureError::FramePoolInitializationFailed, markFault);
    if (!framePoolStepResult) {
        return propagateFailure(framePoolStepResult);
    }

    const auto sessionStartResult =
        runCaptureStartStep([this]() { return session->startSession(); }, "session start",
                            CaptureError::SessionStartFailed, markFault);
    if (!sessionStartResult) {
        return propagateFailure(sessionStartResult);
    }

    {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Running;
        lastError.clear();
    }

    VF_INFO("WinrtCaptureSource started (display index: {})", config.preferredDisplayIndex);
    return {};
#endif
}

std::expected<void, std::error_code> WinrtCaptureSource::stop() {
#ifndef _WIN32
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(stateMutex);
        if (state == CaptureState::Idle) {
            return {};
        }
        state = CaptureState::Stopping;
    }

    std::error_code stopError;
    if (session != nullptr) {
        const std::expected<void, std::error_code> sessionStopResult = session->stop();
        if (!sessionStopResult) {
            stopError = sessionStopResult.error();
        }
    }

    {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Idle;
        if (stopError) {
            lastError = stopError;
        } else {
            lastError.clear();
        }
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    VF_INFO("WinrtCaptureSource stopped");
    return {};
#endif
}

std::expected<void, std::error_code> WinrtCaptureSource::poll() {
    std::scoped_lock lock(stateMutex);
    return pollFaultState(
        state == CaptureState::Fault,
        FaultPollErrors{.lastError = lastError,
                        .fallbackError = makeErrorCode(CaptureError::InvalidState)});
}

#ifdef _WIN32

IWinrtFrameSink* WinrtCaptureSource::trySnapshotRunningSink() {
    std::scoped_lock lock(stateMutex);
    if (state != CaptureState::Running) {
        return nullptr;
    }
    return frameSink;
}

std::optional<WinrtCaptureSource::ArrivedFrame> WinrtCaptureSource::tryAcquireArrivedFrame(
    const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender) {
    const auto frame = sender.TryGetNextFrame();
    if (!frame) {
        return std::nullopt;
    }

    const auto surface = frame.Surface();
    if (!surface) {
        return std::nullopt;
    }

    auto access =
        surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    void* textureRaw = nullptr;
    const HRESULT hr = access->GetInterface(__uuidof(ID3D11Texture2D), &textureRaw);
    if (SUCCEEDED(hr)) {
        texture.attach(static_cast<ID3D11Texture2D*>(textureRaw));
    }
    if (FAILED(hr) || texture == nullptr) {
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    CaptureFrameInfo info;
    info.width = desc.Width;
    info.height = desc.Height;
    info.systemRelativeTime100ns = frame.SystemRelativeTime().count();

    return ArrivedFrame{
        .texture = std::move(texture),
        .info = info,
    };
}

void WinrtCaptureSource::forwardFrameToSink(IWinrtFrameSink& sink, const ArrivedFrame& frame) {
    sink.onFrame(frame.texture.get(), frame.info);
}

void WinrtCaptureSource::markFault(const std::error_code& error) {
    std::scoped_lock lock(stateMutex);
    state = CaptureState::Fault;
    lastError = error;
}

void WinrtCaptureSource::onFrameArrived(
    const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
    const winrt::Windows::Foundation::IInspectable& args) {
    static_cast<void>(args);
    const auto arrivedStartedAt = std::chrono::steady_clock::now();

    IWinrtFrameSink* frameSinkSnapshot = trySnapshotRunningSink();
    if (frameSinkSnapshot == nullptr) {
        return;
    }

    try {
        const std::optional<ArrivedFrame> arrivedFrame = tryAcquireArrivedFrame(sender);
        if (!arrivedFrame.has_value()) {
            if (profiler != nullptr) {
                const auto now = std::chrono::steady_clock::now();
                profiler->recordCpuUs(ProfileStage::CaptureFrameArrived,
                                      static_cast<std::uint64_t>(
                                          std::chrono::duration_cast<std::chrono::microseconds>(
                                              now - arrivedStartedAt)
                                              .count()));
            }
            return;
        }

        const auto forwardStartedAt = std::chrono::steady_clock::now();
        forwardFrameToSink(*frameSinkSnapshot, arrivedFrame.value());
        if (profiler != nullptr) {
            const auto forwardEndedAt = std::chrono::steady_clock::now();
            profiler->recordCpuUs(
                ProfileStage::CaptureFrameForward,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               forwardEndedAt - forwardStartedAt)
                                               .count()));
            profiler->recordCpuUs(
                ProfileStage::CaptureFrameArrived,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               forwardEndedAt - arrivedStartedAt)
                                               .count()));
        }
    } catch (const winrt::hresult_error& ex) {
        markFault(makeErrorCode(CaptureError::InvalidState));
        VF_WARN("WinrtCaptureSource frame processing failed with WinRT exception: {}",
                winrt::to_string(ex.message()));
    } catch (const std::exception& ex) {
        markFault(makeErrorCode(CaptureError::InvalidState));
        VF_WARN("WinrtCaptureSource frame processing failed with exception: {}", ex.what());
    } catch (...) {
        markFault(makeErrorCode(CaptureError::InvalidState));
        VF_WARN("WinrtCaptureSource frame processing failed with unknown exception");
    }
}

#endif

} // namespace vf
