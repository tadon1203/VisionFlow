#include "capture/runtime/capture_runtime_winrt.hpp"

#include <expected>
#include <system_error>

#include <gtest/gtest.h>

#include "VisionFlow/capture/capture_error.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

namespace vf {
namespace {

class DummySink final : public IWinrtFrameSink {
  public:
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override {
        static_cast<void>(texture);
        static_cast<void>(info);
    }
};

TEST(WinrtCaptureRuntimeTest, StartFailsWithInvalidStateWhenSinkIsNotAttached) {
    WinrtCaptureRuntime runtime;

    const auto result = runtime.start(CaptureConfig{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(CaptureError::InvalidState));
}

TEST(WinrtCaptureRuntimeTest, AttachFrameSinkSucceedsWhenSinkInterfaceExists) {
    WinrtCaptureRuntime runtime;
    DummySink sink;

    const auto result = runtime.attachFrameSink(sink);
    EXPECT_TRUE(result.has_value());
}

TEST(WinrtCaptureRuntimeTest, StopIsIdempotentWhenRuntimeIsIdle) {
    WinrtCaptureRuntime runtime;

    const auto first = runtime.stop();
    const auto second = runtime.stop();

    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
}

TEST(WinrtCaptureRuntimeTest, PollSucceedsWhenNoFaultIsPresent) {
    WinrtCaptureRuntime runtime;

    const auto result = runtime.poll();
    EXPECT_TRUE(result.has_value());
}

TEST(WinrtCaptureRuntimeTest, PollSucceedsAfterRecoverableStartValidationFailure) {
    WinrtCaptureRuntime runtime;

    const auto startResult = runtime.start(CaptureConfig{});
    ASSERT_FALSE(startResult.has_value());
    EXPECT_EQ(startResult.error(), makeErrorCode(CaptureError::InvalidState));

    const auto pollResult = runtime.poll();
    EXPECT_TRUE(pollResult.has_value());
}

} // namespace
} // namespace vf
