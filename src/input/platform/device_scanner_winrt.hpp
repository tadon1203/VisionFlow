#pragma once

#include <expected>
#include <string>
#include <system_error>

#include "VisionFlow/input/i_device_scanner.hpp"

namespace vf {

class WinrtDeviceScanner final : public IDeviceScanner {
  public:
    WinrtDeviceScanner() = default;
    WinrtDeviceScanner(const WinrtDeviceScanner&) = default;
    WinrtDeviceScanner(WinrtDeviceScanner&&) = default;
    WinrtDeviceScanner& operator=(const WinrtDeviceScanner&) = default;
    WinrtDeviceScanner& operator=(WinrtDeviceScanner&&) = default;
    ~WinrtDeviceScanner() override = default;

    [[nodiscard]] std::expected<std::string, std::error_code>
    findPortByHardwareId(const std::string& hardwareId) const override;
};

} // namespace vf
