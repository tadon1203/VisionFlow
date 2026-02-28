#pragma once

#include "VisionFlow/input/i_aim_activation_input.hpp"

namespace vf {

class AimActivationInputStub final : public IAimActivationInput {
  public:
    [[nodiscard]] bool isAimActivationPressed() const override { return false; }
};

} // namespace vf
