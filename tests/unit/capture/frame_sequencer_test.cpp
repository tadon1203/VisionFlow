#include "capture/frame_sequencer.hpp"

#include <chrono>
#include <future>
#include <stop_token>

#include <gtest/gtest.h>

namespace vf {
namespace {

TEST(FrameSequencerTest, BackpressureKeepsOnlyLatestFrame) {
    FrameSequencer sequencer;
    sequencer.startAccepting();

    CaptureFrameInfo info1;
    info1.systemRelativeTime100ns = 100;
    CaptureFrameInfo info2;
    info2.systemRelativeTime100ns = 200;
    CaptureFrameInfo info3;
    info3.systemRelativeTime100ns = 300;

    sequencer.submit(nullptr, info1, 1);
    sequencer.submit(nullptr, info2, 2);
    sequencer.submit(nullptr, info3, 3);

    FrameSequencer::PendingFrame out{};
    std::stop_source stopSource;
    ASSERT_TRUE(sequencer.waitAndTakeLatest(stopSource.get_token(), out));

    EXPECT_EQ(out.info.systemRelativeTime100ns, 300);
    EXPECT_EQ(out.fenceValue, 3U);
    EXPECT_EQ(sequencer.droppedFrameCount(), 2U);
}

TEST(FrameSequencerTest, StopsAcceptingFramesImmediately) {
    FrameSequencer sequencer;
    sequencer.startAccepting();
    sequencer.stopAccepting();

    CaptureFrameInfo info;
    info.systemRelativeTime100ns = 123;
    sequencer.submit(nullptr, info, 9);

    FrameSequencer::PendingFrame out{};
    std::stop_source stopSource;
    stopSource.request_stop();

    EXPECT_FALSE(sequencer.waitAndTakeLatest(stopSource.get_token(), out));
    EXPECT_EQ(sequencer.droppedFrameCount(), 0U);
}

TEST(FrameSequencerTest, WakesWaitingWorkerWhenFrameArrives) {
    FrameSequencer sequencer;
    sequencer.startAccepting();

    std::promise<FrameSequencer::PendingFrame> resultPromise;
    std::future<FrameSequencer::PendingFrame> resultFuture = resultPromise.get_future();

    std::jthread worker([&sequencer, &resultPromise](const std::stop_token& stopToken) {
        FrameSequencer::PendingFrame out{};
        if (sequencer.waitAndTakeLatest(stopToken, out)) {
            resultPromise.set_value(out);
        }
    });

    CaptureFrameInfo info;
    info.systemRelativeTime100ns = 777;
    sequencer.submit(nullptr, info, 11);

    const auto status = resultFuture.wait_for(std::chrono::seconds(1));
    ASSERT_EQ(status, std::future_status::ready);

    const auto out = resultFuture.get();
    EXPECT_EQ(out.info.systemRelativeTime100ns, 777);
    EXPECT_EQ(out.fenceValue, 11U);
}

} // namespace
} // namespace vf
