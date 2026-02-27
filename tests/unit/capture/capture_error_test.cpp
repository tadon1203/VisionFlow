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

TEST(CaptureErrorTest, MessageForInferenceRunFailedIsStable) {
    const auto code = makeErrorCode(CaptureError::InferenceRunFailed);
    EXPECT_EQ(code.message(), "inference run failed");
}

TEST(CaptureErrorTest, MessageForInferenceModelNotFoundIsStable) {
    const auto code = makeErrorCode(CaptureError::InferenceModelNotFound);
    EXPECT_EQ(code.message(), "inference model not found");
}

TEST(CaptureErrorTest, MessageForInferenceInterfaceNotSupportedIsStable) {
    const auto code = makeErrorCode(CaptureError::InferenceInterfaceNotSupported);
    EXPECT_EQ(code.message(), "inference interface not supported");
}

} // namespace
} // namespace vf
