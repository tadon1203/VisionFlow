#include "input/makcu/makcu_command_queue.hpp"

#include <cmath>
#include <expected>
#include <limits>
#include <mutex>

#include "VisionFlow/input/mouse_error.hpp"

namespace vf {

void MakcuCommandQueue::reset() {
    std::scoped_lock lock(commandMutex);
    pending = false;
    pendingCommand = {};
    remainder = {0.0F, 0.0F};
    lastInputTime = std::chrono::steady_clock::now();
}

std::expected<void, std::error_code>
MakcuCommandQueue::enqueue(float dx, float dy, std::chrono::milliseconds remainderTtl) {
    bool shouldNotify = false;

    {
        std::scoped_lock lock(commandMutex);
        const auto now = std::chrono::steady_clock::now();
        if (now - lastInputTime > remainderTtl) {
            remainder = {0.0F, 0.0F};
        }

        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            return std::unexpected(makeErrorCode(MouseError::ProtocolError));
        }

        const float accumulatedX = remainder[0] + dx;
        const float accumulatedY = remainder[1] + dy;
        if (!std::isfinite(accumulatedX) || !std::isfinite(accumulatedY)) {
            return std::unexpected(makeErrorCode(MouseError::ProtocolError));
        }

        const double truncatedX = std::trunc(static_cast<double>(accumulatedX));
        const double truncatedY = std::trunc(static_cast<double>(accumulatedY));
        if (truncatedX > static_cast<double>(std::numeric_limits<int>::max()) ||
            truncatedX < static_cast<double>(std::numeric_limits<int>::min()) ||
            truncatedY > static_cast<double>(std::numeric_limits<int>::max()) ||
            truncatedY < static_cast<double>(std::numeric_limits<int>::min())) {
            return std::unexpected(makeErrorCode(MouseError::ProtocolError));
        }

        const int intPartX = static_cast<int>(truncatedX);
        const int intPartY = static_cast<int>(truncatedY);

        remainder[0] = accumulatedX - static_cast<float>(intPartX);
        remainder[1] = accumulatedY - static_cast<float>(intPartY);

        pendingCommand.dx += intPartX;
        pendingCommand.dy += intPartY;
        pending = pendingCommand.dx != 0 || pendingCommand.dy != 0;
        lastInputTime = now;
        shouldNotify = pending;
    }

    if (shouldNotify) {
        commandCv.notify_one();
    }

    return {};
}

bool MakcuCommandQueue::waitAndPop(const std::stop_token& stopToken, MoveCommand& command) {
    std::unique_lock<std::mutex> lock(commandMutex);
    commandCv.wait(lock, [this, &stopToken] { return stopToken.stop_requested() || pending; });
    if (stopToken.stop_requested()) {
        return false;
    }

    command = pendingCommand;
    pendingCommand = {};
    pending = false;
    return true;
}

void MakcuCommandQueue::requeue(int dx, int dy) {
    std::scoped_lock lock(commandMutex);
    pendingCommand.dx += dx;
    pendingCommand.dy += dy;
    pending = pendingCommand.dx != 0 || pendingCommand.dy != 0;

    if (pending) {
        commandCv.notify_one();
    }
}

void MakcuCommandQueue::wakeAll() { commandCv.notify_all(); }

} // namespace vf
