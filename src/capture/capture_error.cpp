#include "VisionFlow/capture/capture_error.hpp"

#include <string_view>
#include <system_error>

namespace vf {

const char* ErrorDomainTraits<CaptureError>::domainName() noexcept { return "capture"; }

std::string_view ErrorDomainTraits<CaptureError>::unknownMessage() noexcept {
    return "unknown capture error";
}

std::string_view ErrorDomainTraits<CaptureError>::message(CaptureError error) noexcept {
    switch (error) {
    case CaptureError::PlatformNotSupported:
        return "platform not supported";
    case CaptureError::InvalidState:
        return "invalid capture state";
    case CaptureError::DisplayNotFound:
        return "display not found";
    case CaptureError::DeviceInitializationFailed:
        return "capture device initialization failed";
    case CaptureError::FramePoolInitializationFailed:
        return "capture frame pool initialization failed";
    case CaptureError::SessionStartFailed:
        return "capture session start failed";
    case CaptureError::SessionStopFailed:
        return "capture session stop failed";
    case CaptureError::InferenceInitializationFailed:
        return "inference initialization failed";
    case CaptureError::InferenceModelInvalid:
        return "inference model is invalid";
    case CaptureError::InferenceGpuInteropFailed:
        return "inference gpu interop failed";
    case CaptureError::InferenceRunFailed:
        return "inference run failed";
    default:
        return {};
    }
}

const std::error_category& captureErrorCategory() noexcept { return errorCategory<CaptureError>(); }

std::error_code makeErrorCode(CaptureError error) noexcept {
    return makeErrorCode<CaptureError>(error);
}

} // namespace vf
