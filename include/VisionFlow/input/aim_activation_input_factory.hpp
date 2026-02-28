#pragma once

#include <memory>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_aim_activation_input.hpp"

namespace vf {

[[nodiscard]] std::unique_ptr<IAimActivationInput>
createAimActivationInput(const VisionFlowConfig& config);

} // namespace vf
