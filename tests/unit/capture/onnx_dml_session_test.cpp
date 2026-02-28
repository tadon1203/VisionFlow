#include "inference/platform/dml/onnx_dml_session.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "VisionFlow/inference/inference_error.hpp"

namespace vf {
namespace {

TEST(OnnxDmlSessionTest, RejectsInputShapeWithBatchGreaterThanOne) {
    const auto metadataResult = OnnxDmlSession::createModelMetadata(
        "input", std::vector<int64_t>{2, 3, 640, 640}, std::vector<std::string>{"output"});

    ASSERT_FALSE(metadataResult.has_value());
    EXPECT_EQ(metadataResult.error(), makeErrorCode(InferenceError::ModelInvalid));
}

TEST(OnnxDmlSessionTest, CreatesMetadataForSingleBatchRgbInput) {
    const auto metadataResult = OnnxDmlSession::createModelMetadata(
        "input", std::vector<int64_t>{1, 3, 640, 640}, std::vector<std::string>{"output"});

    ASSERT_TRUE(metadataResult.has_value());
    constexpr std::size_t kBatch = 1U;
    constexpr std::size_t kChannels = 3U;
    constexpr std::size_t kHeight = 640U;
    constexpr std::size_t kWidth = 640U;
    constexpr std::size_t kElementCount = kBatch * kChannels * kHeight * kWidth;
    EXPECT_EQ(metadataResult->inputName, "input");
    EXPECT_EQ(metadataResult->inputChannels, 3U);
    EXPECT_EQ(metadataResult->inputHeight, 640U);
    EXPECT_EQ(metadataResult->inputWidth, 640U);
    EXPECT_EQ(metadataResult->inputElementCount, kElementCount);
    EXPECT_EQ(metadataResult->inputTensorBytes, kElementCount * sizeof(float));
}

} // namespace
} // namespace vf
