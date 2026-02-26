#include "core/winrt_capture_runtime.hpp"

#include <expected>
#include <memory>
#include <system_error>

#include "capture/debug_capture_processor.hpp"
#include "capture/onnx_dml_capture_processor.hpp"
#include "capture/winrt/i_winrt_frame_sink.hpp"
#include "capture/winrt_capture_source.hpp"

namespace vf {

WinrtCaptureRuntime::WinrtCaptureRuntime(const InferenceConfig& inferenceConfig)
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    : processor(std::make_shared<OnnxDmlCaptureProcessor>(inferenceConfig)),
      source(std::make_unique<WinrtCaptureSource>()) {
#else
    : processor(std::make_shared<DebugCaptureProcessor>()),
      source(std::make_unique<WinrtCaptureSource>()) {
    static_cast<void>(inferenceConfig);
#endif
    source->setFrameSink(processor);
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

    const std::expected<void, std::error_code> processorStopResult = processor->stop();
    if (!processorStopResult && !stopError) {
        stopError = processorStopResult.error();
    }

    if (stopError) {
        return std::unexpected(stopError);
    }
    return {};
}

} // namespace vf
