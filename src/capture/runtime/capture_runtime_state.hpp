#pragma once

#include <cstdint>
#include <expected>
#include <mutex>
#include <system_error>

namespace vf {

class CaptureRuntimeStateMachine {
  public:
    [[nodiscard]] std::expected<void, std::error_code> beforeStart(bool sourceAvailable);
    void onStartSucceeded();
    void onStartFailed();

    [[nodiscard]] std::expected<void, std::error_code> beforeStop();
    void onStopCompleted(bool succeeded);
    [[nodiscard]] std::expected<void, std::error_code> poll() const;

  private:
    enum class RuntimeState : std::uint8_t {
        Idle,
        Starting,
        Running,
        Stopping,
        Fault,
    };

    mutable std::mutex mutex;
    RuntimeState state = RuntimeState::Idle;
};

} // namespace vf
