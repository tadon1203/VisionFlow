#include "VisionFlow/core/app.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/app_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

namespace {

class NoopCaptureRuntime final : public ICaptureRuntime {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config) override {
        static_cast<void>(config);
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> stop() override { return {}; }
};

} // namespace

App::App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig)
    : App(std::move(mouseController), appConfig, CaptureConfig{},
          std::make_unique<NoopCaptureRuntime>()) {}

App::App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig,
         CaptureConfig captureConfig, std::unique_ptr<ICaptureRuntime> captureRuntime)
    : appConfig(appConfig), captureConfig(captureConfig),
      mouseController(std::move(mouseController)), captureRuntime(std::move(captureRuntime)) {}

bool App::run() {
    VF_INFO("App run started");

    if (!mouseController || !captureRuntime) {
        VF_ERROR("App run failed: {}", makeErrorCode(AppError::CompositionFailed).message());
        return false;
    }

    const std::expected<void, std::error_code> captureStartResult =
        captureRuntime->start(captureConfig);
    if (!captureStartResult) {
        VF_ERROR("App run failed: {} ({})", makeErrorCode(AppError::CaptureStartFailed).message(),
                 captureStartResult.error().message());
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

    const std::expected<void, std::error_code> captureStopResult = captureRuntime->stop();
    if (!captureStopResult) {
        VF_WARN("App shutdown warning: capture stop failed ({})",
                captureStopResult.error().message());
    }

    VF_INFO("App run finished");
    return success;
}

void App::tick() const {}

} // namespace vf
