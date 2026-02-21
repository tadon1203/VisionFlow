#pragma once

#include "VisionFlow/input/i_device_scanner.hpp"

namespace vf {

class Win32DeviceScanner final : public IDeviceScanner {
  public:
    Win32DeviceScanner() = default;
    Win32DeviceScanner(const Win32DeviceScanner&) = default;
    Win32DeviceScanner(Win32DeviceScanner&&) = default;
    Win32DeviceScanner& operator=(const Win32DeviceScanner&) = default;
    Win32DeviceScanner& operator=(Win32DeviceScanner&&) = default;
    ~Win32DeviceScanner() override = default;

    [[nodiscard]] std::expected<std::string, std::error_code>
    findPortByHardwareId(const std::string& hardwareId) const override;
};

} // namespace vf
