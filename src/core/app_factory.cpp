#include <memory>
#include <utility>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/core/i_profiler.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"
#include "capture/sources/winrt/capture_source_winrt.hpp"
#include "core/profiler.hpp"
#include "inference/api/winrt_inference_factory.hpp"

namespace vf {

namespace {

struct AppComposition {
    std::unique_ptr<ICaptureSource> captureSource;
    std::unique_ptr<IInferenceProcessor> inferenceProcessor;
    std::unique_ptr<InferenceResultStore> resultStore;
    std::unique_ptr<IProfiler> profiler;
};

AppComposition createAppComposition(const VisionFlowConfig& config) {
    AppComposition composition;

    std::unique_ptr<IProfiler> profiler;
    if (config.profiler.enabled) {
        profiler = std::make_unique<Profiler>(config.profiler);
    }

    auto concreteStore = std::make_unique<InferenceResultStore>();
    auto processorResult =
        createWinrtInferenceProcessor(config.inference, *concreteStore, profiler.get());
    if (!processorResult) {
        VF_ERROR("Failed to create inference processor: {}", processorResult.error().message());
        return {};
    }

    WinrtInferenceBundle inferenceBundle = std::move(processorResult.value());
    auto captureSource =
        std::make_unique<WinrtCaptureSource>(inferenceBundle.frameSink.get(), profiler.get());

    composition.captureSource = std::move(captureSource);
    composition.inferenceProcessor = std::move(inferenceBundle.processor);
    composition.resultStore = std::move(concreteStore);
    composition.profiler = std::move(profiler);
    return composition;
}

} // namespace

App::App(const VisionFlowConfig& config)
    : appConfig(config.app), captureConfig(config.capture),
      mouseController(createMouseController(config)) {
    AppComposition composition = createAppComposition(config);
    captureSource = std::move(composition.captureSource);
    inferenceProcessor = std::move(composition.inferenceProcessor);
    resultStore = std::move(composition.resultStore);
    profiler = std::move(composition.profiler);
}

} // namespace vf
