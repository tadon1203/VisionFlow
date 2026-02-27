#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace vf {

class MakcuAckGate {
  public:
    void reset();
    [[nodiscard]] bool waitUntilSendAllowed(const std::stop_token& stopToken);
    void markAckPending();
    [[nodiscard]] bool waitForAck(const std::stop_token& stopToken,
                                  std::chrono::milliseconds timeout);
    void onDataReceived(std::span<const std::uint8_t> payload, std::string_view ackPrompt,
                        std::size_t ackBufferLimit);
    void clearPendingAndAllowSend();
    void wakeAll();

  private:
    std::condition_variable ackCv;
    std::mutex ackMutex;
    bool sendAllowed = true;
    bool ackPending = false;
    std::string ackBuffer;
};

} // namespace vf
