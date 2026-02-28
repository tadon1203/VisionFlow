#include "core/aim/aim_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace vf {

namespace {

[[nodiscard]] const InferenceDetection*
selectCenterPriorityTarget(const std::vector<InferenceDetection>& detections) {
    const InferenceDetection* selectedTarget = nullptr;
    float bestDistanceSquared = std::numeric_limits<float>::max();
    float bestScore = -std::numeric_limits<float>::infinity();
    constexpr float kModelCenterX = 320.0F;
    constexpr float kModelCenterY = 320.0F;

    for (const InferenceDetection& detection : detections) {
        if (!std::isfinite(detection.centerX) || !std::isfinite(detection.centerY) ||
            !std::isfinite(detection.score)) {
            continue;
        }

        const float deltaX = detection.centerX - kModelCenterX;
        const float deltaY = detection.centerY - kModelCenterY;
        const float distanceSquared = (deltaX * deltaX) + (deltaY * deltaY);
        const bool closerTarget = distanceSquared < bestDistanceSquared;
        const bool sameDistanceBetterScore =
            distanceSquared == bestDistanceSquared && detection.score > bestScore;
        if (selectedTarget == nullptr || closerTarget || sameDistanceBetterScore) {
            selectedTarget = &detection;
            bestDistanceSquared = distanceSquared;
            bestScore = detection.score;
        }
    }

    return selectedTarget;
}

[[nodiscard]] int computeMoveStep(float error, const AimConfig& config) {
    const float scaled = error * config.aimStrength;
    if (!std::isfinite(scaled)) {
        return 0;
    }

    const auto rounded = static_cast<int>(std::lround(scaled));
    const int boundedAimMaxStep = std::clamp(config.aimMaxStep, 1, 127);
    return std::clamp(rounded, -boundedAimMaxStep, boundedAimMaxStep);
}

} // namespace

std::optional<AimMove> computeAimMove(const InferenceResult& result, const AimConfig& config) {
    if (result.detections.empty()) {
        return std::nullopt;
    }

    const InferenceDetection* selectedTarget = selectCenterPriorityTarget(result.detections);
    if (selectedTarget == nullptr) {
        return std::nullopt;
    }

    constexpr float kModelCenterX = 320.0F;
    constexpr float kModelCenterY = 320.0F;
    const float errorX = selectedTarget->centerX - kModelCenterX;
    const float errorY = selectedTarget->centerY - kModelCenterY;
    const int moveX = computeMoveStep(errorX, config);
    const int moveY = computeMoveStep(errorY, config);
    if (moveX == 0 && moveY == 0) {
        return std::nullopt;
    }

    return AimMove{
        .dx = static_cast<float>(moveX),
        .dy = static_cast<float>(moveY),
    };
}

} // namespace vf
