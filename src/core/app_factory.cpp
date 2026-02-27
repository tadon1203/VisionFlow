#include <memory>
#include <utility>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"
#include "capture/runtime/capture_runtime_winrt.hpp"
#include "inference/api/winrt_inference_factory.hpp"
#include "inference/engine/inference_result_store.hpp"

namespace vf {

namespace {

struct AppComposition {
    std::unique_ptr<ICaptureRuntime> captureRuntime;
    std::unique_ptr<IInferenceProcessor> inferenceProcessor;
    std::unique_ptr<IInferenceResultStore> resultStore;
};

AppComposition createAppComposition(const InferenceConfig& config) {
    AppComposition composition;
    auto captureRuntime = std::make_unique<WinrtCaptureRuntime>();
    auto concreteStore = std::make_unique<InferenceResultStore>();
    auto processorResult = createWinrtInferenceProcessor(config, *concreteStore);
    if (!processorResult) {
        VF_ERROR("Failed to create inference processor: {}", processorResult.error().message());
        return {};
    }

    std::unique_ptr<IInferenceProcessor> inferenceProcessor = std::move(processorResult.value());
    auto frameSinkResult = createWinrtInferenceFrameSink(*inferenceProcessor);
    if (!frameSinkResult) {
        VF_ERROR("Failed to create inference frame sink: {}", frameSinkResult.error().message());
        return {};
    }

    captureRuntime->setFrameSink(frameSinkResult.value());

    composition.captureRuntime = std::move(captureRuntime);
    composition.inferenceProcessor = std::move(inferenceProcessor);
    composition.resultStore = std::move(concreteStore);
    return composition;
}

} // namespace

App::App(const VisionFlowConfig& config)
    : appConfig(config.app), captureConfig(config.capture),
      mouseController(createMouseController(config)) {
    AppComposition composition = createAppComposition(config.inference);
    captureRuntime = std::move(composition.captureRuntime);
    inferenceProcessor = std::move(composition.inferenceProcessor);
    resultStore = std::move(composition.resultStore);
}

} // namespace vf
