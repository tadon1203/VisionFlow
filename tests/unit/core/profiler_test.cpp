#include "core/profiler.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vf {
namespace {

TEST(ProfilerTest, MaybeReportSkipsBeforeInterval) {
    ProfilerConfig config;
    config.enabled = true;
    config.reportIntervalMs = std::chrono::milliseconds(1000);

    std::vector<std::string> lines;
    Profiler profiler(config, [&lines](const std::string& line) { lines.push_back(line); });

    const auto base = std::chrono::steady_clock::time_point{};
    profiler.recordCpuUs(ProfileStage::AppTick, 10);
    profiler.maybeReport(base);
    profiler.maybeReport(base + std::chrono::milliseconds(500));

    EXPECT_TRUE(lines.empty());
}

TEST(ProfilerTest, MaybeReportEmitsAndResetsCounters) {
    ProfilerConfig config;
    config.enabled = true;
    config.reportIntervalMs = std::chrono::milliseconds(1000);

    std::vector<std::string> lines;
    Profiler profiler(config, [&lines](const std::string& line) { lines.push_back(line); });

    const auto base = std::chrono::steady_clock::time_point{};
    profiler.recordCpuUs(ProfileStage::AppTick, 10);
    profiler.recordCpuUs(ProfileStage::AppTick, 20);
    profiler.recordGpuUs(ProfileStage::GpuPreprocess, 30);
    profiler.maybeReport(base);
    profiler.maybeReport(base + std::chrono::milliseconds(1000));

    ASSERT_EQ(lines.size(), 1U);
    const std::string& report = lines.front();
    EXPECT_NE(report.find("app.tick count=2 avg=15us max=20us"), std::string::npos);
    EXPECT_NE(report.find("gpu.preprocess count=1 avg=30us max=30us"), std::string::npos);

    profiler.maybeReport(base + std::chrono::milliseconds(2000));
    EXPECT_EQ(lines.size(), 1U);
}

TEST(ProfilerTest, FlushReportEmitsCurrentSnapshot) {
    ProfilerConfig config;
    config.enabled = true;
    config.reportIntervalMs = std::chrono::milliseconds(1000);

    std::vector<std::string> lines;
    Profiler profiler(config, [&lines](const std::string& line) { lines.push_back(line); });

    profiler.recordCpuUs(ProfileStage::CapturePoll, 42);
    profiler.flushReport(std::chrono::steady_clock::time_point{});

    ASSERT_EQ(lines.size(), 1U);
    const std::string& report = lines.front();
    EXPECT_NE(report.find("capture.poll count=1 avg=42us max=42us"), std::string::npos);
}

TEST(ProfilerTest, FlushReportEmitsEventCounters) {
    ProfilerConfig config;
    config.enabled = true;
    config.reportIntervalMs = std::chrono::milliseconds(1000);

    std::vector<std::string> lines;
    Profiler profiler(config, [&lines](const std::string& line) { lines.push_back(line); });

    profiler.recordEvent(ProfileStage::InferenceCollectMiss);
    profiler.recordEvent(ProfileStage::InferenceCollectMiss, 2);
    profiler.flushReport(std::chrono::steady_clock::time_point{});

    ASSERT_EQ(lines.size(), 1U);
    const std::string& report = lines.front();
    EXPECT_NE(report.find("inference.collect_miss events=3"), std::string::npos);
}

} // namespace
} // namespace vf
