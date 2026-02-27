#include "VisionFlow/core/app_error.hpp"

#include <string_view>
#include <system_error>

namespace vf {

const char* ErrorDomainTraits<AppError>::domainName() noexcept { return "app"; }

std::string_view ErrorDomainTraits<AppError>::unknownMessage() noexcept {
    return "unknown app error";
}

std::string_view ErrorDomainTraits<AppError>::message(AppError error) noexcept {
    switch (error) {
    case AppError::CompositionFailed:
        return "app composition failed";
    case AppError::PlatformInitFailed:
        return "platform initialization failed";
    case AppError::CaptureStartFailed:
        return "capture start failed";
    case AppError::InferenceStartFailed:
        return "inference start failed";
    case AppError::InferenceStopFailed:
        return "inference stop failed";
    default:
        return {};
    }
}

const std::error_category& appErrorCategory() noexcept { return errorCategory<AppError>(); }

std::error_code makeErrorCode(AppError error) noexcept { return makeErrorCode<AppError>(error); }

} // namespace vf
