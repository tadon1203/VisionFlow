#include "VisionFlow/inference/inference_error.hpp"

#include <string_view>
#include <system_error>

namespace vf {

const char* ErrorDomainTraits<InferenceError>::domainName() noexcept { return "inference"; }

std::string_view ErrorDomainTraits<InferenceError>::unknownMessage() noexcept {
    return "unknown inference error";
}

std::string_view ErrorDomainTraits<InferenceError>::message(InferenceError error) noexcept {
    switch (error) {
    case InferenceError::PlatformNotSupported:
        return "inference platform not supported";
    case InferenceError::InvalidState:
        return "invalid inference state";
    case InferenceError::InitializationFailed:
        return "inference initialization failed";
    case InferenceError::ModelNotFound:
        return "inference model not found";
    case InferenceError::DeviceLost:
        return "inference device lost";
    case InferenceError::InterfaceNotSupported:
        return "inference interface not supported";
    case InferenceError::ModelInvalid:
        return "inference model is invalid";
    case InferenceError::GpuInteropFailed:
        return "inference gpu interop failed";
    case InferenceError::RunFailed:
        return "inference run failed";
    default:
        return {};
    }
}

const std::error_category& inferenceErrorCategory() noexcept {
    return errorCategory<InferenceError>();
}

std::error_code makeErrorCode(InferenceError error) noexcept {
    return makeErrorCode<InferenceError>(error);
}

} // namespace vf
