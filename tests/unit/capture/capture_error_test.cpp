#include "VisionFlow/capture/capture_error.hpp"

#include <gtest/gtest.h>

namespace vf {
namespace {

TEST(CaptureErrorTest, ErrorCategoryNameIsCapture) {
    EXPECT_STREQ(captureErrorCategory().name(), "capture");
}

TEST(CaptureErrorTest, MessageForDisplayNotFoundIsStable) {
    const auto code = makeErrorCode(CaptureError::DisplayNotFound);
    EXPECT_EQ(code.message(), "display not found");
}

TEST(CaptureErrorTest, MessageForSessionStopFailedIsStable) {
    const auto code = makeErrorCode(CaptureError::SessionStopFailed);
    EXPECT_EQ(code.message(), "capture session stop failed");
}

} // namespace
} // namespace vf
