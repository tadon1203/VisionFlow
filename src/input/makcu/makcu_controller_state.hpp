#pragma once

#include <cstdint>
#include <expected>
#include <mutex>
#include <system_error>

#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

enum class MakcuControllerState : std::uint8_t {
    Idle,
    Opening,
    Ready,
    Stopping,
    Fault,
};

class MakcuStateMachine {
  public:
    [[nodiscard]] std::expected<void, std::error_code> beginConnect();
    [[nodiscard]] bool beginDisconnect();
    void setReady();
    void setIdle();
    void setFault();
    void setDisconnectResult(bool disconnected);

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] IMouseController::State state() const;

  private:
    [[nodiscard]] static IMouseController::State mapState(MakcuControllerState state);

    mutable std::mutex stateMutex;
    MakcuControllerState currentState = MakcuControllerState::Idle;
};

} // namespace vf
