#include "capture/sources/winrt/capture_source_winrt.hpp"

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

TEST(WinrtCaptureSourceTest, StopIsIdempotentWhenSourceIsIdle) {
    DummySink sink;
    WinrtCaptureSource source(sink);

    const auto first = source.stop();
    const auto second = source.stop();

    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
}

TEST(WinrtCaptureSourceTest, StartReturnsPlatformNotSupportedOnNonWindowsWithInjectedSink) {
    DummySink sink;
    WinrtCaptureSource source(sink);

#if defined(_WIN32)
    GTEST_SKIP() << "Non-Windows contract only";
#else
    const auto result = source.start(CaptureConfig{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(CaptureError::PlatformNotSupported));
#endif
}

TEST(WinrtCaptureSourceTest, PollSucceedsWhenNoFaultIsPresent) {
    DummySink sink;
    WinrtCaptureSource source(sink);

    const auto result = source.poll();
    EXPECT_TRUE(result.has_value());
}

} // namespace
} // namespace vf
