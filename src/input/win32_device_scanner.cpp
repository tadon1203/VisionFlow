#include "VisionFlow/input/win32_device_scanner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "VisionFlow/input/mouse_error.hpp"

#if defined(_WIN32)
// clang-format off
#include <Windows.h>
#include <SetupAPI.h>
// clang-format on
#endif

namespace vf {

namespace {

std::string toUpper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    return text;
}

} // namespace

std::expected<std::string, std::error_code> Win32DeviceScanner::findPortByHardwareId(
    const std::string& hardwareId) const {
#if !defined(_WIN32)
    static_cast<void>(hardwareId);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    const HDEVINFO deviceInfo =
        SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortNotFound));
    }

    const std::string target = toUpper(hardwareId);

    SP_DEVINFO_DATA deviceData{};
    deviceData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD index = 0; SetupDiEnumDeviceInfo(deviceInfo, index, &deviceData) != FALSE;
         ++index) {
        std::array<char, 4096> hardwareIdBuffer{};
        DWORD requiredSize = 0;
        const BOOL hardwareIdFound = SetupDiGetDeviceRegistryPropertyA(
            deviceInfo, &deviceData, SPDRP_HARDWAREID, nullptr,
            reinterpret_cast<PBYTE>(hardwareIdBuffer.data()),
            static_cast<DWORD>(hardwareIdBuffer.size()), &requiredSize);
        if (hardwareIdFound == FALSE) {
            continue;
        }

        const std::string deviceHardwareId = toUpper(std::string(hardwareIdBuffer.data()));
        if (deviceHardwareId.find(target) == std::string::npos) {
            continue;
        }

        std::array<char, 1024> friendlyNameBuffer{};
        const BOOL friendlyNameFound = SetupDiGetDeviceRegistryPropertyA(
            deviceInfo, &deviceData, SPDRP_FRIENDLYNAME, nullptr,
            reinterpret_cast<PBYTE>(friendlyNameBuffer.data()),
            static_cast<DWORD>(friendlyNameBuffer.size()), &requiredSize);
        if (friendlyNameFound == FALSE) {
            continue;
        }

        const std::string friendlyName(friendlyNameBuffer.data());
        const std::size_t left = friendlyName.rfind("(COM");
        const std::size_t right = friendlyName.rfind(')');
        if (left == std::string::npos || right == std::string::npos || right <= left + 1U) {
            continue;
        }

        SetupDiDestroyDeviceInfoList(deviceInfo);
        return friendlyName.substr(left + 1U, right - left - 1U);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return std::unexpected(makeErrorCode(MouseError::PortNotFound));
#endif
}

} // namespace vf
