#include "core/winrt_capture_runtime.hpp"

#include <expected>
#include <memory>
#include <system_error>

#include "capture/debug_capture_processor.hpp"
#include "capture/winrt_capture_source.hpp"

namespace vf {

WinrtCaptureRuntime::WinrtCaptureRuntime()
    : processor(std::make_shared<DebugCaptureProcessor>()),
      source(std::make_unique<WinrtCaptureSource>()) {
    source->setProcessor(processor);
}

WinrtCaptureRuntime::~WinrtCaptureRuntime() = default;

std::expected<void, std::error_code> WinrtCaptureRuntime::start(const CaptureConfig& config) {
    return source->start(config);
}

std::expected<void, std::error_code> WinrtCaptureRuntime::stop() { return source->stop(); }

} // namespace vf
