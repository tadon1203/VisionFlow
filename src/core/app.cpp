#include "VisionFlow/core/app.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"
#include "core/expected_utils.hpp"

namespace vf {

namespace {

class NoopCaptureRuntime final : public ICaptureRuntime {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config) override {
        static_cast<void>(config);
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> stop() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> poll() override { return {}; }
};

class NoopInferenceProcessor final : public IInferenceProcessor {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> stop() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> poll() override { return {}; }
};

class NoopInferenceResultStore final : public IInferenceResultStore {
  public:
    void publish(InferenceResult result) override { static_cast<void>(result); }
    [[nodiscard]] std::optional<InferenceResult> take() override { return std::nullopt; }
};

[[nodiscard]] std::expected<void, std::error_code>
logErrorAndPropagate(std::string_view context, const std::error_code& error) {
    VF_ERROR("{} ({})", context, error.message());
    return std::unexpected(error);
}

} // namespace

App::App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig)
    : App(std::move(mouseController), appConfig, CaptureConfig{},
          std::make_unique<NoopCaptureRuntime>(), std::make_unique<NoopInferenceProcessor>(),
          std::make_unique<NoopInferenceResultStore>()) {}

App::App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig,
         CaptureConfig captureConfig, std::unique_ptr<ICaptureRuntime> captureRuntime,
         std::unique_ptr<IInferenceProcessor> inferenceProcessor,
         std::unique_ptr<IInferenceResultStore> resultStore)
    : appConfig(appConfig), captureConfig(captureConfig),
      mouseController(std::move(mouseController)), captureRuntime(std::move(captureRuntime)),
      inferenceProcessor(std::move(inferenceProcessor)), resultStore(std::move(resultStore)) {}

std::expected<void, std::error_code> App::run() {
    VF_INFO("App run started");

    const std::expected<void, std::error_code> setupResult = setup();
    if (!setupResult) {
        return propagateFailure(setupResult);
    }

    const std::expected<void, std::error_code> loopResult = tickLoop();
    shutdown();

    if (!loopResult) {
        return propagateFailure(loopResult);
    }

    VF_INFO("App run finished");
    return {};
}

std::expected<void, std::error_code> App::setup() {
    if (mouseController == nullptr || captureRuntime == nullptr || inferenceProcessor == nullptr ||
        resultStore == nullptr) {
        VF_ERROR("App run failed: required component is null");
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const std::expected<void, std::error_code> inferenceStartResult = inferenceProcessor->start();
    if (!inferenceStartResult) {
        return logErrorAndPropagate("App run failed: inference start failed",
                                    inferenceStartResult.error());
    }

    const std::expected<void, std::error_code> captureStartResult =
        captureRuntime->start(captureConfig);
    if (!captureStartResult) {
        VF_ERROR("App run failed: capture start failed ({})", captureStartResult.error().message());
        const std::expected<void, std::error_code> inferenceStopResult = inferenceProcessor->stop();
        if (!inferenceStopResult) {
            VF_WARN("App setup rollback warning: inference stop failed ({})",
                    inferenceStopResult.error().message());
        }
        return propagateFailure(captureStartResult);
    }

    running = true;
    return {};
}

std::expected<void, std::error_code> App::tickLoop() {
    while (running) {
        const std::expected<void, std::error_code> tickResult = tickOnce();
        if (!tickResult) {
            running = false;
            return propagateFailure(tickResult);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return {};
}

void App::shutdown() {
    const std::expected<void, std::error_code> captureStopResult = captureRuntime->stop();
    if (!captureStopResult) {
        VF_WARN("App shutdown warning: capture stop failed ({})",
                captureStopResult.error().message());
    }

    const std::expected<void, std::error_code> inferenceStopResult = inferenceProcessor->stop();
    if (!inferenceStopResult) {
        VF_WARN("App shutdown warning: inference stop failed ({})",
                inferenceStopResult.error().message());
    }

    const std::expected<void, std::error_code> disconnectResult = mouseController->disconnect();
    if (!disconnectResult) {
        VF_ERROR("App shutdown warning: mouse disconnect failed ({})",
                 disconnectResult.error().message());
    }
}

std::expected<void, std::error_code> App::tickOnce() {
    const std::expected<void, std::error_code> capturePollResult = captureRuntime->poll();
    if (!capturePollResult) {
        return logErrorAndPropagate("App loop failed: capture poll error",
                                    capturePollResult.error());
    }

    const std::expected<void, std::error_code> inferencePollResult = inferenceProcessor->poll();
    if (!inferencePollResult) {
        return logErrorAndPropagate("App loop failed: inference poll error",
                                    inferencePollResult.error());
    }

    const std::expected<void, std::error_code> connectResult = mouseController->connect();
    if (!connectResult) {
        VF_WARN("App reconnect attempt failed: {}", connectResult.error().message());
        if (!mouseController->shouldRetryConnect(connectResult.error())) {
            return logErrorAndPropagate("App run failed: unrecoverable connect error",
                                        connectResult.error());
        }

        std::this_thread::sleep_for(appConfig.reconnectRetryMs);
        return {};
    }

    if (resultStore == nullptr) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const std::optional<InferenceResult> latestResult = resultStore->take();
    if (!latestResult.has_value()) {
        return {};
    }

    return applyInferenceToMouse(*latestResult);
}

std::expected<void, std::error_code> App::applyInferenceToMouse(const InferenceResult& result) {
    static_cast<void>(result);
    return {};
}

} // namespace vf
