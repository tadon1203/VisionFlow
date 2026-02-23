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

App::App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig)
    : appConfig(appConfig), mouseController(std::move(mouseController)) {}

bool App::run() {
    Logger::init();
    VF_INFO("App run started");

    if (!mouseController) {
        VF_ERROR("App run failed: mouse controller is null");
        return false;
    }

    bool success = true;
    running = true;
    while (running) {
        const std::expected<void, std::error_code> connectResult = mouseController->connect();
        if (!connectResult) {
            VF_WARN("App reconnect attempt failed: {}", connectResult.error().message());
            if (!mouseController->shouldRetryConnect(connectResult.error())) {
                VF_ERROR("App run failed: unrecoverable connect error ({})",
                         connectResult.error().message());
                success = false;
                running = false;
                break;
            }

            std::this_thread::sleep_for(appConfig.reconnectRetryMs);
            continue;
        }

        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const std::expected<void, std::error_code> disconnectResult = mouseController->disconnect();
    if (!disconnectResult) {
        VF_ERROR("App shutdown warning: mouse disconnect failed ({})",
                 disconnectResult.error().message());
    }

    VF_INFO("App run finished");
    return success;
}

void App::tick() const {}

} // namespace vf
