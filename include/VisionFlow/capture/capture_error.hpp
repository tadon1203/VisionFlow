#pragma once

#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "VisionFlow/core/error_domain.hpp"

namespace vf {

enum class CaptureError : std::uint8_t {
    PlatformNotSupported = 1,
    InvalidState,
    DisplayNotFound,
    DeviceInitializationFailed,
    FramePoolInitializationFailed,
    SessionStartFailed,
    SessionStopFailed,
    InferenceInitializationFailed,
    InferenceModelNotFound,
    InferenceDeviceLost,
    InferenceInterfaceNotSupported,
    InferenceModelInvalid,
    InferenceGpuInteropFailed,
    InferenceRunFailed,
};

template <> struct ErrorDomainTraits<CaptureError> {
    [[nodiscard]] static const char* domainName() noexcept;
    [[nodiscard]] static std::string_view unknownMessage() noexcept;
    [[nodiscard]] static std::string_view message(CaptureError error) noexcept;
};

[[nodiscard]] const std::error_category& captureErrorCategory() noexcept;
[[nodiscard]] std::error_code makeErrorCode(CaptureError error) noexcept;

} // namespace vf

namespace std {

template <> struct is_error_code_enum<vf::CaptureError> : true_type {};

} // namespace std

namespace vf {

static_assert(StrictErrorDomain<CaptureError>,
              "CaptureError must satisfy StrictErrorDomain (uint8_t enum + error_code_enum + "
              "ErrorDomainTraits).");

} // namespace vf
