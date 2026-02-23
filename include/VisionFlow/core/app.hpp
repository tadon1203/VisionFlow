#pragma once

#include <memory>

#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

class App {
  public:
    explicit App(std::unique_ptr<IMouseController> mouseController);

    bool run();

  private:
    bool running = false;
    std::unique_ptr<IMouseController> mouseController;

    void tick() const;
};

} // namespace vf
