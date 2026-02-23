#include <memory>
#include <utility>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/core/config_loader.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"

int main() {
    vf::Logger::init();

    const auto configResult = vf::loadConfig("config/visionflow.json");
    if (!configResult) {
        VF_ERROR("Failed to load config: {}", configResult.error().message());
        return -1;
    }
    const auto& config = configResult.value();

    auto mouseController = vf::createMouseController(config);
    vf::App app(std::move(mouseController), config.app);
    if (!app.run()) {
        return -1;
    }
    return 0;
}
