#pragma once

#include <memory>

#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

[[nodiscard]] std::unique_ptr<IMouseController> createMouseController();

} // namespace vf
