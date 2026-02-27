#pragma once

#include <memory>

#include "VisionFlow/capture/i_capture_runtime.hpp"
#include "VisionFlow/core/config.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {

class App {
  public:
    explicit App(const VisionFlowConfig& config);
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig);
    App(std::unique_ptr<IMouseController> mouseController, AppConfig appConfig,
        CaptureConfig captureConfig, std::unique_ptr<ICaptureRuntime> captureRuntime,
        std::unique_ptr<IInferenceProcessor> inferenceProcessor,
        std::unique_ptr<IInferenceResultStore> resultStore);

    bool run();

  private:
    bool running = false;
    AppConfig appConfig;
    CaptureConfig captureConfig;
    std::unique_ptr<IMouseController> mouseController;
    std::unique_ptr<ICaptureRuntime> captureRuntime;
    std::unique_ptr<IInferenceProcessor> inferenceProcessor;
    std::unique_ptr<IInferenceResultStore> resultStore;

    bool setup();
    bool tickLoop();
    void shutdown();
    void tick();
    void applyInferenceToMouse(const InferenceResult& result);
};

} // namespace vf
