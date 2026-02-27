#include <memory>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"
#include "capture/runtime/capture_runtime_winrt.hpp"

namespace vf {

App::App(const VisionFlowConfig& config)
    : App(createMouseController(config), config.app, config.capture,
          std::make_unique<WinrtCaptureRuntime>(config.inference)) {}

} // namespace vf
