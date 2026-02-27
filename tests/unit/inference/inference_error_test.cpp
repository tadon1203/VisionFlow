#include "VisionFlow/inference/inference_error.hpp"

#include <gtest/gtest.h>

namespace vf {
namespace {

TEST(InferenceErrorTest, ErrorCategoryNameIsInference) {
    EXPECT_STREQ(inferenceErrorCategory().name(), "inference");
}

TEST(InferenceErrorTest, MessageForRunFailedIsStable) {
    const auto code = makeErrorCode(InferenceError::RunFailed);
    EXPECT_EQ(code.message(), "inference run failed");
}

TEST(InferenceErrorTest, MessageForModelNotFoundIsStable) {
    const auto code = makeErrorCode(InferenceError::ModelNotFound);
    EXPECT_EQ(code.message(), "inference model not found");
}

TEST(InferenceErrorTest, MessageForInterfaceNotSupportedIsStable) {
    const auto code = makeErrorCode(InferenceError::InterfaceNotSupported);
    EXPECT_EQ(code.message(), "inference interface not supported");
}

} // namespace
} // namespace vf
