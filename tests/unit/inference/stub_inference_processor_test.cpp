#include "inference/engine/stub_inference_processor.hpp"

#include <expected>
#include <system_error>

#include <gtest/gtest.h>

#include "VisionFlow/inference/inference_error.hpp"

namespace vf {
namespace {

TEST(StubInferenceProcessorTest, StartReturnsPlatformNotSupported) {
    StubInferenceProcessor processor;

    const auto result = processor.start();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(InferenceError::PlatformNotSupported));
}

TEST(StubInferenceProcessorTest, StopIsIdempotent) {
    StubInferenceProcessor processor;

    EXPECT_TRUE(processor.stop().has_value());
    EXPECT_TRUE(processor.stop().has_value());
}

TEST(StubInferenceProcessorTest, PollSucceeds) {
    StubInferenceProcessor processor;

    EXPECT_TRUE(processor.poll().has_value());
}

} // namespace
} // namespace vf
