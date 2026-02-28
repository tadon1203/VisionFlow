#include "core/aim/aim_controller.hpp"

#include <optional>

#include <gtest/gtest.h>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/inference/inference_result.hpp"

namespace vf {
namespace {

TEST(AimControllerTest, SelectsDetectionClosestToModelCenter) {
    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 500.0F,
        .centerY = 320.0F,
        .width = 10.0F,
        .height = 10.0F,
        .score = 0.95F,
        .classId = 0,
    });
    result.detections.emplace_back(InferenceDetection{
        .centerX = 330.0F,
        .centerY = 320.0F,
        .width = 10.0F,
        .height = 10.0F,
        .score = 0.40F,
        .classId = 0,
    });

    const std::optional<AimMove> move = computeAimMove(result, AimConfig{});
    ASSERT_TRUE(move.has_value());
    EXPECT_FLOAT_EQ(move->dx, 4.0F);
    EXPECT_FLOAT_EQ(move->dy, 0.0F);
}

TEST(AimControllerTest, ClampsToMaxStep) {
    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 640.0F,
        .centerY = 0.0F,
        .width = 10.0F,
        .height = 10.0F,
        .score = 0.95F,
        .classId = 0,
    });

    const std::optional<AimMove> move = computeAimMove(result, AimConfig{});
    ASSERT_TRUE(move.has_value());
    EXPECT_FLOAT_EQ(move->dx, 127.0F);
    EXPECT_FLOAT_EQ(move->dy, -127.0F);
}

TEST(AimControllerTest, ReturnsNoMoveWhenDetectionsAreEmpty) {
    const std::optional<AimMove> move = computeAimMove(InferenceResult{}, AimConfig{});
    EXPECT_FALSE(move.has_value());
}

TEST(AimControllerTest, ReturnsNoMoveWhenRoundedDeltaIsZero) {
    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 320.2F,
        .centerY = 319.9F,
        .width = 10.0F,
        .height = 10.0F,
        .score = 0.95F,
        .classId = 0,
    });

    const std::optional<AimMove> move = computeAimMove(result, AimConfig{});
    EXPECT_FALSE(move.has_value());
}

} // namespace
} // namespace vf
