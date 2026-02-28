#include <memory>
#include <utility>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/core/i_profiler.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"
#include "capture/runtime/capture_runtime_winrt.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"
#include "core/profiler.hpp"
#include "inference/api/winrt_inference_factory.hpp"

namespace vf {

namespace {

struct AppComposition {
    std::unique_ptr<ICaptureRuntime> captureRuntime;
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

    auto captureRuntime = std::make_unique<WinrtCaptureRuntime>(profiler.get());
    auto concreteStore = std::make_unique<InferenceResultStore>();
    auto processorResult =
        createWinrtInferenceProcessor(config.inference, *concreteStore, profiler.get());
    if (!processorResult) {
        VF_ERROR("Failed to create inference processor: {}", processorResult.error().message());
        return {};
    }

    std::unique_ptr<IInferenceProcessor> inferenceProcessor = std::move(processorResult.value());

    auto* frameSink = dynamic_cast<IWinrtFrameSink*>(inferenceProcessor.get());
    if (frameSink == nullptr) {
        VF_ERROR("Failed to attach inference processor: missing IWinrtFrameSink");
        return {};
    }

    const auto attachResult = captureRuntime->attachFrameSink(*frameSink);
    if (!attachResult) {
        VF_ERROR("Failed to attach inference processor: {}", attachResult.error().message());
        return {};
    }

    composition.captureRuntime = std::move(captureRuntime);
    composition.inferenceProcessor = std::move(inferenceProcessor);
    composition.resultStore = std::move(concreteStore);
    composition.profiler = std::move(profiler);
    return composition;
}

} // namespace

App::App(const VisionFlowConfig& config)
    : appConfig(config.app), captureConfig(config.capture),
      mouseController(createMouseController(config)) {
    AppComposition composition = createAppComposition(config);
    captureRuntime = std::move(composition.captureRuntime);
    inferenceProcessor = std::move(composition.inferenceProcessor);
    resultStore = std::move(composition.resultStore);
    profiler = std::move(composition.profiler);
}

} // namespace vf
