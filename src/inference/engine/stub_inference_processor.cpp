#include "inference/engine/stub_inference_processor.hpp"

#include <expected>
#include <system_error>

#include "VisionFlow/inference/inference_error.hpp"

namespace vf {

std::expected<void, std::error_code> StubInferenceProcessor::start() {
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code> StubInferenceProcessor::stop() { return {}; }

std::expected<void, std::error_code> StubInferenceProcessor::poll() { return {}; }

} // namespace vf
