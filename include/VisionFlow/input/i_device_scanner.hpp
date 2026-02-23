#pragma once

#include <expected>
#include <string>
#include <system_error>

namespace vf {

class IDeviceScanner {
  public:
    IDeviceScanner() = default;
    IDeviceScanner(const IDeviceScanner&) = default;
    IDeviceScanner(IDeviceScanner&&) = default;
    IDeviceScanner& operator=(const IDeviceScanner&) = default;
    IDeviceScanner& operator=(IDeviceScanner&&) = default;
    virtual ~IDeviceScanner() = default;

    [[nodiscard]] virtual std::expected<std::string, std::error_code>
    findPortByHardwareId(const std::string& hardwareId) const = 0;
};

} // namespace vf
