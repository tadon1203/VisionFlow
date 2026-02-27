#include "input/makcu/makcu_controller_state.hpp"

#include <expected>
#include <mutex>
#include <system_error>

#include "VisionFlow/input/mouse_error.hpp"

namespace vf {

std::expected<void, std::error_code> MakcuStateMachine::beginConnect() {
    std::scoped_lock lock(stateMutex);
    if (currentState == MakcuControllerState::Ready) {
        return {};
    }
    if (currentState == MakcuControllerState::Opening ||
        currentState == MakcuControllerState::Stopping) {
        return std::unexpected(makeErrorCode(MouseError::ProtocolError));
    }

    currentState = MakcuControllerState::Opening;
    return {};
}

bool MakcuStateMachine::beginDisconnect() {
    std::scoped_lock lock(stateMutex);
    if (currentState == MakcuControllerState::Idle) {
        return false;
    }

    currentState = MakcuControllerState::Stopping;
    return true;
}

void MakcuStateMachine::setReady() {
    std::scoped_lock lock(stateMutex);
    currentState = MakcuControllerState::Ready;
}

void MakcuStateMachine::setIdle() {
    std::scoped_lock lock(stateMutex);
    currentState = MakcuControllerState::Idle;
}

void MakcuStateMachine::setFault() {
    std::scoped_lock lock(stateMutex);
    currentState = MakcuControllerState::Fault;
}

void MakcuStateMachine::setDisconnectResult(bool disconnected) {
    std::scoped_lock lock(stateMutex);
    currentState = disconnected ? MakcuControllerState::Idle : MakcuControllerState::Fault;
}

bool MakcuStateMachine::isReady() const {
    std::scoped_lock lock(stateMutex);
    return currentState == MakcuControllerState::Ready;
}

IMouseController::State MakcuStateMachine::state() const {
    std::scoped_lock lock(stateMutex);
    return mapState(currentState);
}

IMouseController::State MakcuStateMachine::mapState(MakcuControllerState state) {
    switch (state) {
    case MakcuControllerState::Idle:
        return IMouseController::State::Idle;
    case MakcuControllerState::Opening:
        return IMouseController::State::Opening;
    case MakcuControllerState::Ready:
        return IMouseController::State::Ready;
    case MakcuControllerState::Stopping:
        return IMouseController::State::Stopping;
    case MakcuControllerState::Fault:
        return IMouseController::State::Fault;
    }

    return IMouseController::State::Fault;
}

} // namespace vf
