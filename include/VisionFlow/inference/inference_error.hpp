#pragma once

#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "VisionFlow/core/error_domain.hpp"

namespace vf {

enum class InferenceError : std::uint8_t {
    PlatformNotSupported = 1,
    InvalidState,
    InitializationFailed,
    ModelNotFound,
    DeviceLost,
    InterfaceNotSupported,
    ModelInvalid,
    GpuInteropFailed,
    RunFailed,
};

template <> struct ErrorDomainTraits<InferenceError> {
    [[nodiscard]] static const char* domainName() noexcept;
    [[nodiscard]] static std::string_view unknownMessage() noexcept;
    [[nodiscard]] static std::string_view message(InferenceError error) noexcept;
};

[[nodiscard]] const std::error_category& inferenceErrorCategory() noexcept;
[[nodiscard]] std::error_code makeErrorCode(InferenceError error) noexcept;

} // namespace vf

namespace std {

template <> struct is_error_code_enum<vf::InferenceError> : true_type {};

} // namespace std

namespace vf {

static_assert(StrictErrorDomain<InferenceError>,
              "InferenceError must satisfy StrictErrorDomain (uint8_t enum + error_code_enum + "
              "ErrorDomainTraits).");

} // namespace vf
