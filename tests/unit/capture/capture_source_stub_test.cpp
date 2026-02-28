#include "capture/sources/stub/capture_source_stub.hpp"

#include <expected>
#include <system_error>

#include <gtest/gtest.h>

#include "VisionFlow/capture/capture_error.hpp"

namespace vf {
namespace {

TEST(StubCaptureSourceTest, StartReturnsPlatformNotSupported) {
    StubCaptureSource source;

    const auto result = source.start(CaptureConfig{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(CaptureError::PlatformNotSupported));
}

TEST(StubCaptureSourceTest, StopIsIdempotent) {
    StubCaptureSource source;

    EXPECT_TRUE(source.stop().has_value());
    EXPECT_TRUE(source.stop().has_value());
}

TEST(StubCaptureSourceTest, PollSucceeds) {
    StubCaptureSource source;

    EXPECT_TRUE(source.poll().has_value());
}

} // namespace
} // namespace vf
