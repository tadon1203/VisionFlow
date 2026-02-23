#pragma once

#include <memory>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

[[nodiscard]] std::unique_ptr<IMouseController>
createMouseController(const VisionFlowConfig& config);

} // namespace vf
