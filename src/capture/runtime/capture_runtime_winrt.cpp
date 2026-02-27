#include "capture/runtime/capture_runtime_winrt.hpp"

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "capture/sources/winrt/capture_source_winrt.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

namespace vf {

WinrtCaptureRuntime::WinrtCaptureRuntime() : source(std::make_unique<WinrtCaptureSource>()) {}

WinrtCaptureRuntime::~WinrtCaptureRuntime() = default;

std::expected<void, std::error_code> WinrtCaptureRuntime::start(const CaptureConfig& config) {
    if (frameSink == nullptr || source == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }
    return source->start(config);
}

std::expected<void, std::error_code> WinrtCaptureRuntime::stop() {
    if (source != nullptr) {
        source->setFrameSink(nullptr);
    }

    std::error_code stopError;

    const std::expected<void, std::error_code> sourceStopResult =
        source != nullptr ? source->stop() : std::expected<void, std::error_code>{};
    if (!sourceStopResult) {
        stopError = sourceStopResult.error();
    }

    if (stopError) {
        return std::unexpected(stopError);
    }
    return {};
}

std::expected<void, std::error_code>
WinrtCaptureRuntime::attachInferenceProcessor(IInferenceProcessor& processor) {
    IWinrtFrameSink* sink = dynamic_cast<IWinrtFrameSink*>(&processor);
    if (sink == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInterfaceNotSupported));
    }

    setFrameSink(sink);
    return {};
}

void WinrtCaptureRuntime::setFrameSink(IWinrtFrameSink* nextFrameSink) {
    frameSink = nextFrameSink;
    if (source != nullptr) {
        source->setFrameSink(frameSink);
    }
}

} // namespace vf
