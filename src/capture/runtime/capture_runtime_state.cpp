#include "capture/runtime/capture_runtime_state.hpp"

#include <expected>
#include <mutex>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"

namespace vf {

std::expected<void, std::error_code> CaptureRuntimeStateMachine::beforeAttachSink() {
    std::scoped_lock lock(mutex);
    if (state == RuntimeState::Stopping) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }
    return {};
}

std::expected<void, std::error_code> CaptureRuntimeStateMachine::beforeStart(bool sinkAttached,
                                                                             bool sourceAvailable) {
    std::scoped_lock lock(mutex);
    if (state == RuntimeState::Running) {
        return {};
    }
    if (state == RuntimeState::Starting || state == RuntimeState::Stopping) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }
    if (!sinkAttached || !sourceAvailable) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }
    state = RuntimeState::Starting;
    return {};
}

void CaptureRuntimeStateMachine::onStartSucceeded() {
    std::scoped_lock lock(mutex);
    state = RuntimeState::Running;
}

void CaptureRuntimeStateMachine::onStartFailed() {
    std::scoped_lock lock(mutex);
    state = RuntimeState::Fault;
}

std::expected<void, std::error_code> CaptureRuntimeStateMachine::beforeStop() {
    std::scoped_lock lock(mutex);
    if (state == RuntimeState::Idle) {
        return {};
    }
    if (state == RuntimeState::Stopping) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }
    state = RuntimeState::Stopping;
    return {};
}

void CaptureRuntimeStateMachine::onStopCompleted(bool succeeded) {
    std::scoped_lock lock(mutex);
    state = succeeded ? RuntimeState::Idle : RuntimeState::Fault;
}

} // namespace vf
