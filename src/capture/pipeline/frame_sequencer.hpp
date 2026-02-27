#pragma once

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace vf {

template <typename TFrame> class FrameSequencer {
  public:
    static_assert(std::movable<TFrame>, "FrameSequencer requires movable frame type");

    void startAccepting() {
        std::scoped_lock lock(frameMutex);
        droppedFrames = 0;
        pendingFrame.reset();
        isRunning.store(true, std::memory_order_release);
    }

    void stopAccepting() {
        isRunning.store(false, std::memory_order_release);
        frameCv.notify_all();
    }

    void submit(TFrame frame) {
        if (!isRunning.load(std::memory_order_acquire)) {
            return;
        }

        std::scoped_lock lock(frameMutex);
        if (!isRunning.load(std::memory_order_relaxed)) {
            return;
        }

        if (pendingFrame.has_value()) {
            ++droppedFrames;
        }

        pendingFrame = std::move(frame);
        frameCv.notify_one();
    }

    [[nodiscard]] bool waitAndTakeLatest(const std::stop_token& stopToken, TFrame& outFrame) {
        std::unique_lock lock(frameMutex);
        frameCv.wait(lock, [this, &stopToken] {
            return pendingFrame.has_value() || stopToken.stop_requested() ||
                   !isRunning.load(std::memory_order_acquire);
        });

        // Drain the last pending frame even when shutdown/stop is requested.
        if (pendingFrame.has_value()) {
            outFrame = std::move(*pendingFrame);
            pendingFrame.reset();
            return true;
        }

        return false;
    }

    void clear() {
        std::scoped_lock lock(frameMutex);
        pendingFrame.reset();
    }

    [[nodiscard]] std::size_t droppedFrameCount() const {
        std::scoped_lock lock(frameMutex);
        return droppedFrames;
    }

  private:
    std::atomic<bool> isRunning{false};
    mutable std::mutex frameMutex;
    std::condition_variable frameCv;

    std::optional<TFrame> pendingFrame;
    std::size_t droppedFrames = 0;
};

} // namespace vf
