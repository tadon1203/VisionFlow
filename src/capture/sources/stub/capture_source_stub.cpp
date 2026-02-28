#include "capture/sources/stub/capture_source_stub.hpp"

#include <expected>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"

namespace vf {

std::expected<void, std::error_code> StubCaptureSource::start(const CaptureConfig& config) {
    static_cast<void>(config);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<void, std::error_code> StubCaptureSource::stop() { return {}; }

std::expected<void, std::error_code> StubCaptureSource::poll() { return {}; }

} // namespace vf
