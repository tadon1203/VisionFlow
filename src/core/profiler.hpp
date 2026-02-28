#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/core/i_profiler.hpp"

namespace vf {

class Profiler final : public IProfiler {
  public:
    using ReportSink = std::function<void(const std::string&)>;

    explicit Profiler(const ProfilerConfig& config, ReportSink reportSink = {});

    void recordCpuUs(ProfileStage stage, std::uint64_t microseconds) override;
    void recordGpuUs(ProfileStage stage, std::uint64_t microseconds) override;
    void recordEvent(ProfileStage stage, std::uint64_t count = 1) override;
    void maybeReport(std::chrono::steady_clock::time_point now) override;
    void flushReport(std::chrono::steady_clock::time_point now) override;

  private:
    struct StageCounters {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> sumUs{0};
        std::atomic<std::uint64_t> maxUs{0};
    };

    struct StageSnapshot {
        std::uint64_t count = 0;
        std::uint64_t sumUs = 0;
        std::uint64_t maxUs = 0;
    };

    struct EventCounters {
        std::atomic<std::uint64_t> count{0};
    };

    static constexpr std::size_t kStageCount = static_cast<std::size_t>(ProfileStage::Count);

    void record(ProfileStage stage, std::uint64_t microseconds);
    std::string buildReportLine(std::chrono::steady_clock::time_point now, bool includeEmpty);
    StageSnapshot snapshotAndReset(ProfileStage stage);
    std::uint64_t snapshotEventsAndReset(ProfileStage stage);

    std::array<StageCounters, kStageCount> stageCounters{};
    std::array<EventCounters, kStageCount> eventCounters{};
    std::chrono::milliseconds reportInterval{1000};
    std::chrono::steady_clock::time_point lastReportAt;
    bool hasLastReportAt = false;
    ReportSink reportSink;
};

} // namespace vf
