#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>

#include "capture/capture_frame_info.hpp"

#ifdef _WIN32
#include <d3d11.h>
#include <winrt/base.h>
#endif

namespace vf {

class FrameSequencer {
  public:
    struct PendingFrame {
        CaptureFrameInfo info;
#ifdef _WIN32
        winrt::com_ptr<ID3D11Texture2D> texture;
#endif
        std::uint64_t fenceValue = 0;
    };

    void startAccepting();
    void stopAccepting();

#ifdef _WIN32
    void submit(ID3D11Texture2D* texture, const CaptureFrameInfo& info, std::uint64_t fenceValue);
#else
    void submit(void* texture, const CaptureFrameInfo& info, std::uint64_t fenceValue);
#endif

    [[nodiscard]] bool waitAndTakeLatest(const std::stop_token& stopToken, PendingFrame& outFrame);
    void clear();

    [[nodiscard]] std::size_t droppedFrameCount() const;

  private:
    std::atomic<bool> isRunning{false};
    mutable std::mutex frameMutex;
    std::condition_variable frameCv;

    bool hasPendingFrame = false;
    PendingFrame pendingFrame{};
    std::size_t droppedFrames = 0;
};

} // namespace vf
