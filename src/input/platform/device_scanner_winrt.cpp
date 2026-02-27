#include "input/platform/device_scanner_winrt.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <system_error>

#include "VisionFlow/input/mouse_error.hpp"
#include "input/string_utils.hpp"

#ifdef _WIN32
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.SerialCommunication.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>
#endif

namespace vf {

std::expected<std::string, std::error_code>
WinrtDeviceScanner::findPortByHardwareId(const std::string& hardwareId) const {
#ifndef _WIN32
    static_cast<void>(hardwareId);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    namespace wde = winrt::Windows::Devices::Enumeration;
    namespace wds = winrt::Windows::Devices::SerialCommunication;

    try {
        const auto targetHardwareId = input::detail::toUpper(hardwareId);
        const winrt::hstring selector = wds::SerialDevice::GetDeviceSelector();
        const auto devices = wde::DeviceInformation::FindAllAsync(selector).get();

        const std::uint32_t deviceCount = devices.Size();
        for (std::uint32_t index = 0; index < deviceCount; ++index) {
            const auto device = devices.GetAt(index);
            const std::string id = input::detail::toUpper(winrt::to_string(device.Id()));
            if (!id.contains(targetHardwareId)) {
                continue;
            }

            try {
                const auto serialDevice = wds::SerialDevice::FromIdAsync(device.Id()).get();
                if (!serialDevice) {
                    continue;
                }

                std::string portName = winrt::to_string(serialDevice.PortName());
                if (!portName.empty()) {
                    return portName;
                }
            } catch (const winrt::hresult_error&) {
                continue;
            }
        }

        return std::unexpected(makeErrorCode(MouseError::PortNotFound));
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(MouseError::PortNotFound));
    }
#endif
}

} // namespace vf
