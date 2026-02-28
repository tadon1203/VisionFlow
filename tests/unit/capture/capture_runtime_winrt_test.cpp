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

TEST(WinrtCaptureRuntimeTest, StartReturnsPlatformNotSupportedOnNonWindowsWithInjectedSink) {
    DummySink sink;
    WinrtCaptureRuntime runtime(sink);

#if defined(_WIN32)
    GTEST_SKIP() << "Non-Windows contract only";
#else
    const auto result = runtime.start(CaptureConfig{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(CaptureError::PlatformNotSupported));
#endif
}

TEST(WinrtCaptureRuntimeTest, StopIsIdempotentWhenRuntimeIsIdle) {
    DummySink sink;
    WinrtCaptureRuntime runtime(sink);

    const auto first = runtime.stop();
    const auto second = runtime.stop();

    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
}

TEST(WinrtCaptureRuntimeTest, PollSucceedsWhenNoFaultIsPresent) {
    DummySink sink;
    WinrtCaptureRuntime runtime(sink);

    const auto result = runtime.poll();
    EXPECT_TRUE(result.has_value());
}

TEST(WinrtCaptureRuntimeTest, PollSucceedsAfterStartAttempt) {
    DummySink sink;
    WinrtCaptureRuntime runtime(sink);

    const auto startResult = runtime.start(CaptureConfig{});
#if defined(_WIN32)
    EXPECT_TRUE(startResult.has_value());
#else
    ASSERT_FALSE(startResult.has_value());
    EXPECT_EQ(startResult.error(), makeErrorCode(CaptureError::PlatformNotSupported));
#endif

    const auto pollResult = runtime.poll();
    EXPECT_TRUE(pollResult.has_value());
}

} // namespace
} // namespace vf
