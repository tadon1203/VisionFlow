#include "VisionFlow/core/app.hpp"
#include "VisionFlow/core/app_error.hpp"
#include "VisionFlow/core/config_loader.hpp"
#include "VisionFlow/core/logger.hpp"
#include "platform/winrt/platform_context_winrt.hpp"

int main() {
    vf::Logger::init();

    const auto configResult = vf::loadConfig("config/visionflow.json");
    if (!configResult) {
        VF_ERROR("Failed to load config: {}", configResult.error().message());
        return -1;
    }

    vf::WinRtPlatformContext platformContext;
    const auto platformInitResult = platformContext.initialize();
    if (!platformInitResult) {
        VF_ERROR("Failed to initialize platform runtime: {} ({})",
                 vf::makeErrorCode(vf::AppError::PlatformInitFailed).message(),
                 platformInitResult.error().message());
        return -1;
    }

    vf::App app(configResult.value());
    const auto runResult = app.run();
    if (!runResult) {
        VF_ERROR("App run failed: {}", runResult.error().message());
        return -1;
    }
    return 0;
}
