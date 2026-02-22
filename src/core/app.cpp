#include "VisionFlow/core/app.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

App::App(std::unique_ptr<IMouseController> mouseController)
    : mouseController(std::move(mouseController)) {}

bool App::run() {
    Logger::init();
    VF_INFO("App run started");

    if (!mouseController) {
        VF_ERROR("App run failed: mouse controller is null");
        return false;
    }

    const std::expected<void, std::error_code> connectResult = mouseController->connect();
    if (!connectResult) {
        VF_ERROR("App run failed: mouse connect failed ({})", connectResult.error().message());
        return false;
    }

    running = true;

    mainLoop();

    const std::expected<void, std::error_code> disconnectResult = mouseController->disconnect();
    if (!disconnectResult) {
        VF_ERROR("App shutdown warning: mouse disconnect failed ({})",
                 disconnectResult.error().message());
    }

    VF_INFO("App run finished");
    return true;
}

void App::mainLoop() const {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace vf
