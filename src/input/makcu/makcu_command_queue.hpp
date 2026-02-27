#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <mutex>
#include <stop_token>
#include <system_error>

namespace vf {

class MakcuCommandQueue {
  public:
    struct MoveCommand {
        int dx = 0;
        int dy = 0;
    };

    MakcuCommandQueue() = default;
    MakcuCommandQueue(const MakcuCommandQueue&) = delete;
    MakcuCommandQueue(MakcuCommandQueue&&) = delete;
    MakcuCommandQueue& operator=(const MakcuCommandQueue&) = delete;
    MakcuCommandQueue& operator=(MakcuCommandQueue&&) = delete;
    ~MakcuCommandQueue() = default;

    void reset();

    [[nodiscard]] std::expected<void, std::error_code>
    enqueue(float dx, float dy, std::chrono::milliseconds remainderTtl);

    [[nodiscard]] bool waitAndPop(const std::stop_token& stopToken, MoveCommand& command);

    void requeue(int dx, int dy);
    void wakeAll();

  private:
    std::condition_variable commandCv;
    std::mutex commandMutex;
    bool pending = false;
    MoveCommand pendingCommand;
    std::array<float, 2> remainder{0.0F, 0.0F};
    std::chrono::steady_clock::time_point lastInputTime = std::chrono::steady_clock::now();
};

} // namespace vf
