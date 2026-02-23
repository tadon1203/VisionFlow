#pragma once

#include <memory>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

class App {
  public:
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig);

    bool run();

  private:
    bool running = false;
    AppConfig appConfig;
    std::unique_ptr<IMouseController> mouseController;

    void tick() const;
};

} // namespace vf
