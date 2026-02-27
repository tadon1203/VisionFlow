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
    EXPECT_EQ(metadataResult->inputName, "input");
    EXPECT_EQ(metadataResult->inputChannels, 3U);
    EXPECT_EQ(metadataResult->inputHeight, 640U);
    EXPECT_EQ(metadataResult->inputWidth, 640U);
    EXPECT_EQ(metadataResult->inputElementCount, static_cast<std::size_t>(1 * 3 * 640 * 640));
    EXPECT_EQ(metadataResult->inputTensorBytes,
              static_cast<std::size_t>(1 * 3 * 640 * 640 * sizeof(float)));
}

} // namespace
} // namespace vf
