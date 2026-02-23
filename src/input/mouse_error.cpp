#include "VisionFlow/input/mouse_error.hpp"

#include <string_view>
#include <system_error>

namespace vf {

const char* ErrorDomainTraits<MouseError>::domainName() noexcept { return "mouse"; }

std::string_view ErrorDomainTraits<MouseError>::unknownMessage() noexcept {
    return "unknown mouse error";
}

std::string_view ErrorDomainTraits<MouseError>::message(MouseError error) noexcept {
    switch (error) {
    case MouseError::PlatformNotSupported:
        return "platform not supported";
    case MouseError::PortNotFound:
        return "target COM port not found";
    case MouseError::PortOpenFailed:
        return "failed to open COM port";
    case MouseError::ConfigureDcbFailed:
        return "failed to configure serial DCB";
    case MouseError::WriteFailed:
        return "serial write failed";
    case MouseError::ReadFailed:
        return "serial read failed";
    case MouseError::HandshakeTimeout:
        return "handshake timed out";
    case MouseError::ProtocolError:
        return "protocol error";
    case MouseError::NotConnected:
        return "not connected";
    case MouseError::ThreadNotRunning:
        return "sender thread not running";
    default:
        return {};
    }
}

const std::error_category& mouseErrorCategory() noexcept { return errorCategory<MouseError>(); }

std::error_code makeErrorCode(MouseError error) noexcept {
    return makeErrorCode<MouseError>(error);
}

bool shouldRetryConnectError(const std::error_code& error) noexcept {
    if (error.category() != mouseErrorCategory()) {
        return false;
    }

    const auto code = static_cast<MouseError>(error.value());
    return code != MouseError::PlatformNotSupported;
}

} // namespace vf
