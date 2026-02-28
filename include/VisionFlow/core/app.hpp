#pragma once

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/capture/i_capture_source.hpp"
#include "VisionFlow/core/config.hpp"
#include "VisionFlow/core/i_profiler.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

class App {
  public:
    explicit App(const VisionFlowConfig& config);
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig,
        CaptureConfig captureConfig, std::unique_ptr<ICaptureSource> captureSource,
        std::unique_ptr<IInferenceProcessor> inferenceProcessor,
        std::unique_ptr<InferenceResultStore> resultStore);
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig,
        CaptureConfig captureConfig, std::unique_ptr<ICaptureSource> captureSource,
        std::unique_ptr<IInferenceProcessor> inferenceProcessor,
        std::unique_ptr<InferenceResultStore> resultStore, std::unique_ptr<IProfiler> profiler);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    [[nodiscard]] std::expected<void, std::error_code> run();

  private:
    bool running = false;
    AppConfig appConfig;
    CaptureConfig captureConfig;
    std::unique_ptr<IMouseController> mouseController;
    std::unique_ptr<ICaptureSource> captureSource;
    std::unique_ptr<IInferenceProcessor> inferenceProcessor;
    std::unique_ptr<InferenceResultStore> resultStore;
    std::unique_ptr<IProfiler> profiler;

    [[nodiscard]] std::expected<void, std::error_code> setup();
    [[nodiscard]] std::expected<void, std::error_code> tickLoop();
    void shutdown();
    [[nodiscard]] std::expected<void, std::error_code> tickOnce();
    [[nodiscard]] static std::expected<void, std::error_code>
    applyInferenceToMouse(const InferenceResult& result);
};

} // namespace vf
