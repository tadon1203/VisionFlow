#include "capture/runtime/capture_runtime_winrt.hpp"

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/inference/i_inference_processor.hpp"
#include "capture/sources/winrt/capture_source_winrt.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"
#include "inference/api/winrt_inference_factory.hpp"

namespace vf {

WinrtCaptureRuntime::WinrtCaptureRuntime(const InferenceConfig& inferenceConfig)
    : source(std::make_unique<WinrtCaptureSource>()) {
    WinrtInferencePipeline pipeline = createWinrtInferencePipeline(inferenceConfig);
    processor = std::move(pipeline.processor);
    frameSink = std::move(pipeline.frameSink);
    source->setFrameSink(frameSink);
}

WinrtCaptureRuntime::~WinrtCaptureRuntime() = default;

std::expected<void, std::error_code> WinrtCaptureRuntime::start(const CaptureConfig& config) {
    const std::expected<void, std::error_code> processorStartResult = processor->start();
    if (!processorStartResult) {
        return std::unexpected(processorStartResult.error());
    }

    const std::expected<void, std::error_code> sourceStartResult = source->start(config);
    if (!sourceStartResult) {
        const std::expected<void, std::error_code> processorStopResult = processor->stop();
        if (!processorStopResult) {
            return std::unexpected(processorStopResult.error());
        }
        return std::unexpected(sourceStartResult.error());
    }

    return {};
}

std::expected<void, std::error_code> WinrtCaptureRuntime::stop() {
    std::error_code stopError;

    const std::expected<void, std::error_code> sourceStopResult = source->stop();
    if (!sourceStopResult) {
        stopError = sourceStopResult.error();
    }

    const std::expected<void, std::error_code> processorStopResult =
        processor != nullptr ? processor->stop() : std::expected<void, std::error_code>{};
    if (!processorStopResult && !stopError) {
        stopError = processorStopResult.error();
    }

    if (stopError) {
        return std::unexpected(stopError);
    }
    return {};
}

} // namespace vf
