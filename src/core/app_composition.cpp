#include <memory>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"
#include "core/winrt_capture_runtime.hpp"

namespace vf {

App::App(const VisionFlowConfig& config)
    : App(createMouseController(config), config.app, config.capture,
          std::make_unique<WinrtCaptureRuntime>()) {}

} // namespace vf
