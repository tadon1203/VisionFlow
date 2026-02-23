#pragma once

#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "VisionFlow/core/error_domain.hpp"

namespace vf {

enum class MouseError : std::uint8_t {
    PlatformNotSupported = 1,
    PortNotFound,
    PortOpenFailed,
    ConfigureDcbFailed,
    WriteFailed,
    ReadFailed,
    HandshakeTimeout,
    ProtocolError,
    NotConnected,
    ThreadNotRunning,
};

template <> struct ErrorDomainTraits<MouseError> {
    [[nodiscard]] static const char* domainName() noexcept;
    [[nodiscard]] static std::string_view unknownMessage() noexcept;
    [[nodiscard]] static std::string_view message(MouseError error) noexcept;
};

[[nodiscard]] const std::error_category& mouseErrorCategory() noexcept;
[[nodiscard]] std::error_code makeErrorCode(MouseError error) noexcept;
[[nodiscard]] bool shouldRetryConnectError(const std::error_code& error) noexcept;

} // namespace vf

namespace std {

template <> struct is_error_code_enum<vf::MouseError> : true_type {};

} // namespace std

namespace vf {

static_assert(StrictErrorDomain<MouseError>,
              "MouseError must satisfy StrictErrorDomain (uint8_t enum + error_code_enum + "
              "ErrorDomainTraits).");

} // namespace vf
