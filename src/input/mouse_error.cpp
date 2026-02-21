#include "VisionFlow/input/mouse_error.hpp"

#include <string>

namespace vf {

namespace {

class MouseErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "mouse"; }

    [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<MouseError>(value)) {
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
        case MouseError::ThreadNotRunning:
            return "sender thread not running";
        default:
            return "unknown mouse error";
        }
    }
};

} // namespace

const std::error_category& mouseErrorCategory() noexcept {
    static MouseErrorCategory category;
    return category;
}

std::error_code makeErrorCode(MouseError error) noexcept {
    return {static_cast<int>(error), mouseErrorCategory()};
}

} // namespace vf
