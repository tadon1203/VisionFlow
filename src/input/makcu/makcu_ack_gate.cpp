#include "input/makcu/makcu_ack_gate.hpp"

#include <condition_variable>
#include <mutex>

namespace vf {

void MakcuAckGate::reset() {
    std::scoped_lock lock(ackMutex);
    sendAllowed = true;
    ackPending = false;
    ackBuffer.clear();
}

bool MakcuAckGate::waitUntilSendAllowed(const std::stop_token& stopToken) {
    std::unique_lock<std::mutex> lock(ackMutex);
    ackCv.wait(lock, [this, &stopToken] { return stopToken.stop_requested() || sendAllowed; });
    return !stopToken.stop_requested();
}

void MakcuAckGate::markAckPending() {
    std::scoped_lock lock(ackMutex);
    sendAllowed = false;
    ackPending = true;
}

bool MakcuAckGate::waitForAck(const std::stop_token& stopToken, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(ackMutex);
    const bool ackReceived = ackCv.wait_for(
        lock, timeout, [this, &stopToken] { return stopToken.stop_requested() || !ackPending; });

    if (stopToken.stop_requested()) {
        return false;
    }
    if (!ackReceived || ackPending) {
        ackPending = false;
        sendAllowed = true;
        return false;
    }
    return true;
}

void MakcuAckGate::onDataReceived(std::span<const std::uint8_t> payload, std::string_view ackPrompt,
                                  std::size_t ackBufferLimit) {
    std::scoped_lock lock(ackMutex);

    ackBuffer.append(reinterpret_cast<const char*>(payload.data()), payload.size());

    const std::size_t ackPosition = ackBuffer.find(ackPrompt);
    if (ackPosition != std::string::npos) {
        if (ackPending) {
            ackPending = false;
            sendAllowed = true;
            ackCv.notify_one();
        }
        ackBuffer.erase(0, ackPosition + ackPrompt.size());
        return;
    }

    if (ackBuffer.size() > ackBufferLimit) {
        ackBuffer.erase(0, ackBuffer.size() - ackBufferLimit);
    }
}

void MakcuAckGate::clearPendingAndAllowSend() {
    {
        std::scoped_lock lock(ackMutex);
        ackPending = false;
        sendAllowed = true;
    }
    ackCv.notify_one();
}

void MakcuAckGate::wakeAll() { ackCv.notify_all(); }

} // namespace vf
