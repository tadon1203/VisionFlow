#pragma once

#include <memory>

#include "VisionFlow/capture/i_capture_runtime.hpp"
#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

class App {
  public:
    explicit App(const VisionFlowConfig& config);
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig);
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig,
        CaptureConfig captureConfig, std::unique_ptr<ICaptureRuntime> captureRuntime);

    bool run();

  private:
    bool running = false;
    AppConfig appConfig;
    CaptureConfig captureConfig;
    std::unique_ptr<IMouseController> mouseController;
    std::unique_ptr<ICaptureRuntime> captureRuntime;

    void tick() const;
};

} // namespace vf
