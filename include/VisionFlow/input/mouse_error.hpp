#pragma once

#include <cstdint>
#include <system_error>
#include <type_traits>

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

[[nodiscard]] const std::error_category& mouseErrorCategory() noexcept;
[[nodiscard]] std::error_code makeErrorCode(MouseError error) noexcept;
[[nodiscard]] bool shouldRetryConnectError(const std::error_code& error) noexcept;

} // namespace vf

namespace std {

template <> struct is_error_code_enum<vf::MouseError> : true_type {};

} // namespace std
