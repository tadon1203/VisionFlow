#pragma once

#include <expected>
#include <system_error>

#include "VisionFlow/input/mouse_error.hpp"

namespace vf {

class IMouseController {
  public:
    IMouseController() = default;
    IMouseController(const IMouseController&) = default;
    IMouseController(IMouseController&&) = default;
    IMouseController& operator=(const IMouseController&) = default;
    IMouseController& operator=(IMouseController&&) = default;
    virtual ~IMouseController() = default;

    [[nodiscard]] virtual std::expected<void, std::error_code> connect() = 0;
    [[nodiscard]] virtual bool shouldRetryConnect(const std::error_code& error) const {
        return shouldRetryConnectError(error);
    }
    [[nodiscard]] virtual std::expected<void, std::error_code> disconnect() = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code> move(float dx, float dy) = 0;
};

} // namespace vf
