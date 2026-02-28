#pragma once

#include <chrono>
#include <cstdint>

namespace vf {

enum class ProfileStage : std::uint8_t {
    AppTick,
    CapturePoll,
    InferencePoll,
    ConnectAttempt,
    ApplyInference,
    CaptureFrameArrived,
    CaptureFrameForward,
    InferenceInitialize,
    InferenceEnqueue,
    InferenceCollect,
    InferenceCollectMiss,
    InferenceEnqueueSkipped,
    InferencePreprocess,
    InferenceRun,
    InferencePostprocess,
    GpuPreprocess,
    Count,
};

class IProfiler {
  public:
    virtual ~IProfiler() = default;
    IProfiler(const IProfiler&) = delete;
    IProfiler& operator=(const IProfiler&) = delete;
    IProfiler(IProfiler&&) = delete;
    IProfiler& operator=(IProfiler&&) = delete;

    virtual void recordCpuUs(ProfileStage stage, std::uint64_t microseconds) = 0;
    virtual void recordGpuUs(ProfileStage stage, std::uint64_t microseconds) = 0;
    virtual void recordEvent(ProfileStage stage, std::uint64_t count = 1) = 0;
    virtual void maybeReport(std::chrono::steady_clock::time_point now) = 0;
    virtual void flushReport(std::chrono::steady_clock::time_point now) = 0;

  protected:
    IProfiler() = default;
};

} // namespace vf
