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
    InferencePreprocess,
    InferenceRun,
    GpuPreprocess,
    Count,
};

class IProfiler {
  public:
    virtual ~IProfiler() = default;

    virtual void recordCpuUs(ProfileStage stage, std::uint64_t microseconds) = 0;
    virtual void recordGpuUs(ProfileStage stage, std::uint64_t microseconds) = 0;
    virtual void maybeReport(std::chrono::steady_clock::time_point now) = 0;
    virtual void flushReport(std::chrono::steady_clock::time_point now) = 0;
};

} // namespace vf
