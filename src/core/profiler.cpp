#include "core/profiler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "VisionFlow/core/logger.hpp"

namespace vf {
namespace {

constexpr std::string_view stageName(ProfileStage stage) {
    switch (stage) {
    case ProfileStage::AppTick:
        return "app.tick";
    case ProfileStage::CapturePoll:
        return "capture.poll";
    case ProfileStage::InferencePoll:
        return "inference.poll";
    case ProfileStage::ConnectAttempt:
        return "connect.attempt";
    case ProfileStage::ApplyInference:
        return "apply.inference";
    case ProfileStage::CaptureFrameArrived:
        return "capture.frame_arrived";
    case ProfileStage::CaptureFrameForward:
        return "capture.frame_forward";
    case ProfileStage::InferenceInitialize:
        return "inference.initialize";
    case ProfileStage::InferenceEnqueue:
        return "inference.enqueue";
    case ProfileStage::InferenceCollect:
        return "inference.collect";
    case ProfileStage::InferenceCollectMiss:
        return "inference.collect_miss";
    case ProfileStage::InferenceEnqueueSkipped:
        return "inference.enqueue_skipped";
    case ProfileStage::InferencePreprocess:
        return "inference.preprocess";
    case ProfileStage::InferenceRun:
        return "inference.run";
    case ProfileStage::GpuPreprocess:
        return "gpu.preprocess";
    case ProfileStage::Count:
        break;
    }
    return "unknown";
}

} // namespace

Profiler::Profiler(const ProfilerConfig& config, ReportSink reportSink)
    : reportInterval(config.reportIntervalMs), reportSink(std::move(reportSink)) {}

void Profiler::recordCpuUs(ProfileStage stage, std::uint64_t microseconds) {
    record(stage, microseconds);
}

void Profiler::recordGpuUs(ProfileStage stage, std::uint64_t microseconds) {
    record(stage, microseconds);
}

void Profiler::recordEvent(ProfileStage stage, std::uint64_t count) {
    const auto index = static_cast<std::size_t>(stage);
    if (index >= kStageCount) {
        return;
    }

    eventCounters.at(index).count.fetch_add(count, std::memory_order_relaxed);
}

void Profiler::maybeReport(std::chrono::steady_clock::time_point now) {
    if (!hasLastReportAt) {
        hasLastReportAt = true;
        lastReportAt = now;
        return;
    }

    if ((now - lastReportAt) < reportInterval) {
        return;
    }

    std::string line = buildReportLine(now, false);
    lastReportAt = now;
    if (line.empty()) {
        return;
    }

    if (reportSink) {
        reportSink(line);
        return;
    }
    VF_INFO("{}", line);
}

void Profiler::flushReport(std::chrono::steady_clock::time_point now) {
    std::string line = buildReportLine(now, false);
    if (line.empty()) {
        return;
    }
    if (reportSink) {
        reportSink(line);
        return;
    }
    VF_INFO("{}", line);
}

void Profiler::record(ProfileStage stage, std::uint64_t microseconds) {
    const auto index = static_cast<std::size_t>(stage);
    if (index >= kStageCount) {
        return;
    }

    StageCounters& counters = stageCounters.at(index);
    counters.count.fetch_add(1, std::memory_order_relaxed);
    counters.sumUs.fetch_add(microseconds, std::memory_order_relaxed);

    std::uint64_t currentMax = counters.maxUs.load(std::memory_order_relaxed);
    while (microseconds > currentMax &&
           !counters.maxUs.compare_exchange_weak(
               currentMax, microseconds, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

std::string Profiler::buildReportLine(std::chrono::steady_clock::time_point now,
                                      bool includeEmpty) {
    const auto nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string line = std::format("[prof] interval={}ms now={}ms", reportInterval.count(), nowMs);

    bool hasAnyStage = false;
    constexpr std::array<ProfileStage, kStageCount> kStages = {
        ProfileStage::AppTick,
        ProfileStage::CapturePoll,
        ProfileStage::InferencePoll,
        ProfileStage::ConnectAttempt,
        ProfileStage::ApplyInference,
        ProfileStage::CaptureFrameArrived,
        ProfileStage::CaptureFrameForward,
        ProfileStage::InferenceInitialize,
        ProfileStage::InferenceEnqueue,
        ProfileStage::InferenceCollect,
        ProfileStage::InferenceCollectMiss,
        ProfileStage::InferenceEnqueueSkipped,
        ProfileStage::InferencePreprocess,
        ProfileStage::InferenceRun,
        ProfileStage::GpuPreprocess,
    };

    for (const ProfileStage stage : kStages) {
        const StageSnapshot snapshot = snapshotAndReset(stage);
        const std::uint64_t events = snapshotEventsAndReset(stage);
        if (!includeEmpty && snapshot.count == 0 && events == 0) {
            continue;
        }

        if (snapshot.count > 0) {
            const std::uint64_t averageUs = snapshot.sumUs / snapshot.count;
            line.append(std::format(" | {} count={} avg={}us max={}us", stageName(stage),
                                    snapshot.count, averageUs, snapshot.maxUs));
        } else {
            line.append(std::format(" | {}", stageName(stage)));
        }

        if (events > 0) {
            line.append(std::format(" events={}", events));
        }
        hasAnyStage = true;
    }

    if (!hasAnyStage) {
        return {};
    }
    return line;
}

Profiler::StageSnapshot Profiler::snapshotAndReset(ProfileStage stage) {
    const auto index = static_cast<std::size_t>(stage);
    StageCounters& counters = stageCounters.at(index);

    StageSnapshot snapshot;
    snapshot.count = counters.count.exchange(0, std::memory_order_relaxed);
    snapshot.sumUs = counters.sumUs.exchange(0, std::memory_order_relaxed);
    snapshot.maxUs = counters.maxUs.exchange(0, std::memory_order_relaxed);
    return snapshot;
}

std::uint64_t Profiler::snapshotEventsAndReset(ProfileStage stage) {
    const auto index = static_cast<std::size_t>(stage);
    return eventCounters.at(index).count.exchange(0, std::memory_order_relaxed);
}

} // namespace vf
