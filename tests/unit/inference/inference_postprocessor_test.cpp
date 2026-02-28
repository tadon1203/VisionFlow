#include "inference/engine/inference_postprocessor.hpp"

#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "VisionFlow/inference/inference_error.hpp"

namespace vf {
namespace {

constexpr std::size_t kAnchorCount = 8400U;

[[nodiscard]] InferenceResult makeResultWithOutput0() {
    InferenceResult result;
    InferenceTensor tensor;
    tensor.name = "output0";
    tensor.shape = {1, 5, static_cast<int64_t>(kAnchorCount)};
    tensor.values.assign(1U * 5U * kAnchorCount, 0.0F);
    result.tensors.emplace_back(std::move(tensor));
    return result;
}

void setCandidate(InferenceResult& result, std::size_t index, float centerX, float centerY,
                  float width, float height, float score) {
    ASSERT_FALSE(result.tensors.empty());
    ASSERT_LT(index, kAnchorCount);
    std::vector<float>& values = result.tensors.at(0).values;
    values.at(index) = centerX;
    values.at(kAnchorCount + index) = centerY;
    values.at((2U * kAnchorCount) + index) = width;
    values.at((3U * kAnchorCount) + index) = height;
    values.at((4U * kAnchorCount) + index) = score;
}

TEST(InferencePostprocessorTest, DecodesDetectionsAndAppliesConfidenceThreshold) {
    InferenceResult result = makeResultWithOutput0();
    setCandidate(result, 0U, 100.0F, 200.0F, 40.0F, 20.0F, 0.9F);
    setCandidate(result, 1U, 50.0F, 60.0F, 20.0F, 20.0F, 0.1F);

    InferencePostprocessor postprocessor;
    const auto processResult = postprocessor.process(result);

    ASSERT_TRUE(processResult.has_value());
    ASSERT_EQ(result.detections.size(), 1U);
    const InferenceDetection& detection = result.detections.at(0);
    EXPECT_FLOAT_EQ(detection.centerX, 100.0F);
    EXPECT_FLOAT_EQ(detection.centerY, 200.0F);
    EXPECT_FLOAT_EQ(detection.width, 40.0F);
    EXPECT_FLOAT_EQ(detection.height, 20.0F);
    EXPECT_FLOAT_EQ(detection.score, 0.9F);
    EXPECT_EQ(detection.classId, 0);
}

TEST(InferencePostprocessorTest, AppliesNmsToOverlappingDetections) {
    InferenceResult result = makeResultWithOutput0();
    setCandidate(result, 0U, 320.0F, 320.0F, 100.0F, 100.0F, 0.95F);
    setCandidate(result, 1U, 320.0F, 320.0F, 100.0F, 100.0F, 0.90F);

    InferencePostprocessor postprocessor;
    const auto processResult = postprocessor.process(result);

    ASSERT_TRUE(processResult.has_value());
    ASSERT_EQ(result.detections.size(), 1U);
    EXPECT_FLOAT_EQ(result.detections.at(0).score, 0.95F);
}

TEST(InferencePostprocessorTest, RejectsUnexpectedOutputTensorName) {
    InferenceResult result = makeResultWithOutput0();
    result.tensors.at(0).name = "scores";

    InferencePostprocessor postprocessor;
    const auto processResult = postprocessor.process(result);

    ASSERT_FALSE(processResult.has_value());
    EXPECT_EQ(processResult.error(), makeErrorCode(InferenceError::ModelInvalid));
}

TEST(InferencePostprocessorTest, RejectsUnexpectedOutputTensorShape) {
    InferenceResult result = makeResultWithOutput0();
    result.tensors.at(0).shape = {1, 6, static_cast<int64_t>(kAnchorCount)};

    InferencePostprocessor postprocessor;
    const auto processResult = postprocessor.process(result);

    ASSERT_FALSE(processResult.has_value());
    EXPECT_EQ(processResult.error(), makeErrorCode(InferenceError::ModelInvalid));
}

} // namespace
} // namespace vf
