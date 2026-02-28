#include "capture/runtime/capture_runtime_winrt.hpp"

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"
#include "capture/runtime/capture_runtime_state.hpp"
#include "capture/sources/winrt/capture_source_winrt.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"
#include "core/expected_utils.hpp"

namespace vf {

WinrtCaptureRuntime::WinrtCaptureRuntime(IWinrtFrameSink& frameSink, IProfiler* profiler)
    : source(std::make_unique<WinrtCaptureSource>(frameSink, profiler)),
      runtimeState(std::make_unique<CaptureRuntimeStateMachine>()) {}

WinrtCaptureRuntime::~WinrtCaptureRuntime() = default;

std::expected<void, std::error_code> WinrtCaptureRuntime::start(const CaptureConfig& config) {
    if (runtimeState == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    const auto beforeStartResult = runtimeState->beforeStart(source != nullptr);
    if (!beforeStartResult) {
        return propagateFailure(beforeStartResult);
    }

    const auto sourceStartResult =
        source != nullptr ? source->start(config) : std::expected<void, std::error_code>{};
    if (!sourceStartResult) {
        runtimeState->onStartFailed();
        return propagateFailure(sourceStartResult);
    }

    runtimeState->onStartSucceeded();
    return {};
}

std::expected<void, std::error_code> WinrtCaptureRuntime::stop() {
    if (runtimeState == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    const auto beforeStopResult = runtimeState->beforeStop();
    if (!beforeStopResult) {
        return propagateFailure(beforeStopResult);
    }

    std::error_code stopError;

    const std::expected<void, std::error_code> sourceStopResult =
        source != nullptr ? source->stop() : std::expected<void, std::error_code>{};
    if (!sourceStopResult) {
        stopError = sourceStopResult.error();
    }

    runtimeState->onStopCompleted(!stopError);

    if (stopError) {
        return std::unexpected(stopError);
    }

    return {};
}

std::expected<void, std::error_code> WinrtCaptureRuntime::poll() {
    if (runtimeState == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    const auto runtimePollResult = runtimeState->poll();
    if (!runtimePollResult) {
        return propagateFailure(runtimePollResult);
    }

    if (source == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    return source->poll();
}

} // namespace vf
