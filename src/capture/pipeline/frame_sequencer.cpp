#include "capture/pipeline/frame_sequencer.hpp"

#include <stop_token>

namespace vf {

void FrameSequencer::startAccepting() {
    std::scoped_lock lock(frameMutex);
    droppedFrames = 0;
    hasPendingFrame = false;
    pendingFrame = {};
    isRunning.store(true, std::memory_order_release);
}

void FrameSequencer::stopAccepting() {
    isRunning.store(false, std::memory_order_release);
    frameCv.notify_all();
}

void FrameSequencer::submit(
#ifdef _WIN32
    ID3D11Texture2D* texture,
#else
    void* texture,
#endif
    const CaptureFrameInfo& info, std::uint64_t fenceValue) {
    if (!isRunning.load(std::memory_order_acquire)) {
        return;
    }

    std::scoped_lock lock(frameMutex);
    if (!isRunning.load(std::memory_order_relaxed)) {
        return;
    }

    if (hasPendingFrame) {
        ++droppedFrames;
    }

#ifdef _WIN32
    pendingFrame.texture.copy_from(texture);
#else
    static_cast<void>(texture);
#endif
    pendingFrame.info = info;
    pendingFrame.fenceValue = fenceValue;
    hasPendingFrame = true;
    frameCv.notify_one();
}

bool FrameSequencer::waitAndTakeLatest(const std::stop_token& stopToken, PendingFrame& outFrame) {
    std::unique_lock lock(frameMutex);
    frameCv.wait(lock, [this, &stopToken] {
        return hasPendingFrame || stopToken.stop_requested() ||
               !isRunning.load(std::memory_order_acquire);
    });

    // Drain the last pending frame even when shutdown/stop is requested.
    if (hasPendingFrame) {
        outFrame = pendingFrame;
        hasPendingFrame = false;
        return true;
    }

    return false;
}

void FrameSequencer::clear() {
    std::scoped_lock lock(frameMutex);
    hasPendingFrame = false;
    pendingFrame = {};
}

std::size_t FrameSequencer::droppedFrameCount() const {
    std::scoped_lock lock(frameMutex);
    return droppedFrames;
}

} // namespace vf
