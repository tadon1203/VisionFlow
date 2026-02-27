#pragma once

#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "VisionFlow/core/error_domain.hpp"

namespace vf {

enum class AppError : std::uint8_t {
    CompositionFailed = 1,
    PlatformInitFailed,
    CaptureStartFailed,
    InferenceStartFailed,
    InferenceStopFailed,
};

template <> struct ErrorDomainTraits<AppError> {
    [[nodiscard]] static const char* domainName() noexcept;
    [[nodiscard]] static std::string_view unknownMessage() noexcept;
    [[nodiscard]] static std::string_view message(AppError error) noexcept;
};

[[nodiscard]] const std::error_category& appErrorCategory() noexcept;
[[nodiscard]] std::error_code makeErrorCode(AppError error) noexcept;

} // namespace vf

namespace std {

template <> struct is_error_code_enum<vf::AppError> : true_type {};

} // namespace std

namespace vf {

static_assert(StrictErrorDomain<AppError>,
              "AppError must satisfy StrictErrorDomain (uint8_t enum + error_code_enum + "
              "ErrorDomainTraits).");

} // namespace vf
