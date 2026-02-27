#include "VisionFlow/core/app.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/app_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"
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
        return std::unexpected(setupResult.error());
    }

    const std::expected<void, std::error_code> loopResult = tickLoop();
    shutdown();

    if (!loopResult) {
        return std::unexpected(loopResult.error());
    }

    VF_INFO("App run finished");
    return {};
}

std::expected<void, std::error_code> App::setup() {
    if (mouseController == nullptr || captureRuntime == nullptr || inferenceProcessor == nullptr ||
        resultStore == nullptr) {
        VF_ERROR("App run failed: {}", makeErrorCode(AppError::CompositionFailed).message());
        return std::unexpected(makeErrorCode(AppError::CompositionFailed));
    }

    const std::expected<void, std::error_code> inferenceStartResult = inferenceProcessor->start();
    if (!inferenceStartResult) {
        VF_ERROR("App run failed: {} ({})", makeErrorCode(AppError::InferenceStartFailed).message(),
                 inferenceStartResult.error().message());
        return std::unexpected(makeErrorCode(AppError::InferenceStartFailed));
    }

    const std::expected<void, std::error_code> captureStartResult =
        captureRuntime->start(captureConfig);
    if (!captureStartResult) {
        VF_ERROR("App run failed: {} ({})", makeErrorCode(AppError::CaptureStartFailed).message(),
                 captureStartResult.error().message());
        const std::expected<void, std::error_code> inferenceStopResult = inferenceProcessor->stop();
        if (!inferenceStopResult) {
            VF_WARN("App setup rollback warning: inference stop failed ({})",
                    inferenceStopResult.error().message());
        }
        return std::unexpected(makeErrorCode(AppError::CaptureStartFailed));
    }

    running = true;
    return {};
}

std::expected<void, std::error_code> App::tickLoop() {
    while (running) {
        const std::expected<void, std::error_code> capturePollResult = captureRuntime->poll();
        if (!capturePollResult) {
            VF_ERROR("App loop failed: capture poll error ({})",
                     capturePollResult.error().message());
            running = false;
            return std::unexpected(capturePollResult.error());
        }

        const std::expected<void, std::error_code> inferencePollResult = inferenceProcessor->poll();
        if (!inferencePollResult) {
            VF_ERROR("App loop failed: inference poll error ({})",
                     inferencePollResult.error().message());
            running = false;
            return std::unexpected(inferencePollResult.error());
        }

        const std::expected<void, std::error_code> connectResult = mouseController->connect();
        if (!connectResult) {
            VF_WARN("App reconnect attempt failed: {}", connectResult.error().message());
            if (!mouseController->shouldRetryConnect(connectResult.error())) {
                VF_ERROR("App run failed: unrecoverable connect error ({})",
                         connectResult.error().message());
                running = false;
                return std::unexpected(connectResult.error());
            }

            std::this_thread::sleep_for(appConfig.reconnectRetryMs);
            continue;
        }

        const std::expected<void, std::error_code> tickResult = tick();
        if (!tickResult) {
            running = false;
            return std::unexpected(tickResult.error());
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
        VF_WARN("App shutdown warning: {} ({})",
                makeErrorCode(AppError::InferenceStopFailed).message(),
                inferenceStopResult.error().message());
    }

    const std::expected<void, std::error_code> disconnectResult = mouseController->disconnect();
    if (!disconnectResult) {
        VF_ERROR("App shutdown warning: mouse disconnect failed ({})",
                 disconnectResult.error().message());
    }
}

std::expected<void, std::error_code> App::tick() {
    if (resultStore == nullptr) {
        return std::unexpected(makeErrorCode(AppError::CompositionFailed));
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
