#pragma once

#include <optional>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/inference/inference_result.hpp"

namespace vf {

struct AimMove {
    float dx = 0.0F;
    float dy = 0.0F;
};

[[nodiscard]] std::optional<AimMove> computeAimMove(const InferenceResult& result,
                                                    const AimConfig& config);

} // namespace vf
