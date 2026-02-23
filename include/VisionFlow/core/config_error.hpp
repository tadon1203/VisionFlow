#pragma once

#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "VisionFlow/core/error_domain.hpp"

namespace vf {

enum class ConfigError : std::uint8_t {
    FileNotFound = 1,
    ParseFailed,
    MissingKey,
    InvalidType,
    OutOfRange,
};

template <> struct ErrorDomainTraits<ConfigError> {
    [[nodiscard]] static const char* domainName() noexcept;
    [[nodiscard]] static std::string_view unknownMessage() noexcept;
    [[nodiscard]] static std::string_view message(ConfigError error) noexcept;
};

[[nodiscard]] const std::error_category& configErrorCategory() noexcept;
[[nodiscard]] std::error_code makeErrorCode(ConfigError error) noexcept;

} // namespace vf

namespace std {

template <> struct is_error_code_enum<vf::ConfigError> : true_type {};

} // namespace std

namespace vf {

static_assert(StrictErrorDomain<ConfigError>,
              "ConfigError must satisfy StrictErrorDomain (uint8_t enum + error_code_enum + "
              "ErrorDomainTraits).");

} // namespace vf
