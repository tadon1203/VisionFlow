#pragma once

#include <expected>
#include <string>
#include <system_error>

#include "VisionFlow/input/i_device_scanner.hpp"

namespace vf {

class WinRtDeviceScanner final : public IDeviceScanner {
  public:
    WinRtDeviceScanner() = default;
    WinRtDeviceScanner(const WinRtDeviceScanner&) = default;
    WinRtDeviceScanner(WinRtDeviceScanner&&) = default;
    WinRtDeviceScanner& operator=(const WinRtDeviceScanner&) = default;
    WinRtDeviceScanner& operator=(WinRtDeviceScanner&&) = default;
    ~WinRtDeviceScanner() override = default;

    [[nodiscard]] std::expected<std::string, std::error_code>
    findPortByHardwareId(const std::string& hardwareId) const override;
};

} // namespace vf
