#include "capture/pipeline/frame_sequencer.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stop_token>

#include <gtest/gtest.h>

namespace vf {
namespace {

struct TestFrame {
    std::int64_t systemRelativeTime100ns = 0;
    std::uint64_t fenceValue = 0;
};

struct MoveOnlyTestFrame {
    std::unique_ptr<int> payload;
    std::uint64_t fenceValue = 0;
};

TEST(FrameSequencerTest, BackpressureKeepsOnlyLatestFrame) {
    FrameSequencer<TestFrame> sequencer;
    sequencer.startAccepting();

    TestFrame frame1{.systemRelativeTime100ns = 100, .fenceValue = 1};
    TestFrame frame2{.systemRelativeTime100ns = 200, .fenceValue = 2};
    TestFrame frame3{.systemRelativeTime100ns = 300, .fenceValue = 3};

    sequencer.submit(frame1);
    sequencer.submit(frame2);
    sequencer.submit(frame3);

    TestFrame out{};
    std::stop_source stopSource;
    ASSERT_TRUE(sequencer.waitAndTakeLatest(stopSource.get_token(), out));

    EXPECT_EQ(out.systemRelativeTime100ns, 300);
    EXPECT_EQ(out.fenceValue, 3U);
    EXPECT_EQ(sequencer.droppedFrameCount(), 2U);
}

TEST(FrameSequencerTest, StopsAcceptingFramesImmediately) {
    FrameSequencer<TestFrame> sequencer;
    sequencer.startAccepting();
    sequencer.stopAccepting();

    sequencer.submit(TestFrame{.systemRelativeTime100ns = 123, .fenceValue = 9});

    TestFrame out{};
    std::stop_source stopSource;
    stopSource.request_stop();

    EXPECT_FALSE(sequencer.waitAndTakeLatest(stopSource.get_token(), out));
    EXPECT_EQ(sequencer.droppedFrameCount(), 0U);
}

TEST(FrameSequencerTest, WakesWaitingWorkerWhenFrameArrives) {
    FrameSequencer<TestFrame> sequencer;
    sequencer.startAccepting();

    std::promise<TestFrame> resultPromise;
    std::future<TestFrame> resultFuture = resultPromise.get_future();

    std::jthread worker([&sequencer, &resultPromise](const std::stop_token& stopToken) {
        TestFrame out{};
        if (sequencer.waitAndTakeLatest(stopToken, out)) {
            resultPromise.set_value(out);
        }
    });

    sequencer.submit(TestFrame{.systemRelativeTime100ns = 777, .fenceValue = 11});

    const auto status = resultFuture.wait_for(std::chrono::seconds(1));
    ASSERT_EQ(status, std::future_status::ready);

    const auto out = resultFuture.get();
    EXPECT_EQ(out.systemRelativeTime100ns, 777);
    EXPECT_EQ(out.fenceValue, 11U);
}

TEST(FrameSequencerTest, SupportsMoveOnlyFrameType) {
    FrameSequencer<MoveOnlyTestFrame> sequencer;
    sequencer.startAccepting();

    MoveOnlyTestFrame frame;
    frame.payload = std::make_unique<int>(42);
    frame.fenceValue = 7;
    sequencer.submit(std::move(frame));

    MoveOnlyTestFrame out{};
    std::stop_source stopSource;
    ASSERT_TRUE(sequencer.waitAndTakeLatest(stopSource.get_token(), out));

    ASSERT_NE(out.payload, nullptr);
    EXPECT_EQ(*out.payload, 42);
    EXPECT_EQ(out.fenceValue, 7U);
}

} // namespace
} // namespace vf
