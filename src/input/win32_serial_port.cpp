#include "VisionFlow/input/win32_serial_port.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "VisionFlow/input/mouse_error.hpp"

namespace vf {

Win32SerialPort::~Win32SerialPort() {
    const std::expected<void, std::error_code> result = close();
    static_cast<void>(result);
}

std::string Win32SerialPort::makeComPath(const std::string& portName) {
    return "\\\\.\\" + portName;
}

std::expected<void, std::error_code> Win32SerialPort::open(const std::string& portName,
                                                            std::uint32_t baudRate) {
#if !defined(_WIN32)
    static_cast<void>(portName);
    static_cast<void>(baudRate);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    {
        std::lock_guard<std::mutex> lock(handleMutex);

        if (opened) {
            return {};
        }

        serialHandle = CreateFileA(makeComPath(portName).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (serialHandle == INVALID_HANDLE_VALUE) {
            return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
        }

        COMMTIMEOUTS timeouts {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        if (SetCommTimeouts(serialHandle, &timeouts) == FALSE) {
            CloseHandle(serialHandle);
            serialHandle = INVALID_HANDLE_VALUE;
            return std::unexpected(makeErrorCode(MouseError::ConfigureDcbFailed));
        }

        if (PurgeComm(serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR) == FALSE) {
            CloseHandle(serialHandle);
            serialHandle = INVALID_HANDLE_VALUE;
            return std::unexpected(makeErrorCode(MouseError::ConfigureDcbFailed));
        }

        opened = true;
    }

    const std::expected<void, std::error_code> configureResult = configure(baudRate);
    if (!configureResult) {
        const std::expected<void, std::error_code> closeResult = close();
        if (!closeResult) {
            return std::unexpected(closeResult.error());
        }
        return std::unexpected(configureResult.error());
    }
    return {};
#endif
}

std::expected<void, std::error_code> Win32SerialPort::close() {
#if !defined(_WIN32)
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::lock_guard<std::mutex> lock(handleMutex);

    if (!opened) {
        return {};
    }

    if (serialHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(serialHandle);
        serialHandle = INVALID_HANDLE_VALUE;
    }
    opened = false;

    return {};
#endif
}

std::expected<void, std::error_code> Win32SerialPort::configure(std::uint32_t baudRate) {
#if !defined(_WIN32)
    static_cast<void>(baudRate);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::lock_guard<std::mutex> lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    DCB dcb {};
    dcb.DCBlength = sizeof(DCB);

    if (GetCommState(serialHandle, &dcb) == FALSE) {
        return std::unexpected(makeErrorCode(MouseError::ConfigureDcbFailed));
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;

    if (SetCommState(serialHandle, &dcb) == FALSE) {
        return std::unexpected(makeErrorCode(MouseError::ConfigureDcbFailed));
    }

    return {};
#endif
}

std::expected<void, std::error_code> Win32SerialPort::flush() {
#if !defined(_WIN32)
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::lock_guard<std::mutex> lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    if (PurgeComm(serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR) == FALSE) {
        return std::unexpected(makeErrorCode(MouseError::ReadFailed));
    }

    return {};
#endif
}

std::expected<void, std::error_code> Win32SerialPort::write(
    std::span<const std::uint8_t> payload) {
#if !defined(_WIN32)
    static_cast<void>(payload);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::lock_guard<std::mutex> lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    DWORD bytesWritten = 0;
    const BOOL writeCompleted =
        WriteFile(serialHandle, payload.data(), static_cast<DWORD>(payload.size()), &bytesWritten,
                  nullptr);
    if (writeCompleted == FALSE || static_cast<std::size_t>(bytesWritten) != payload.size()) {
        return std::unexpected(makeErrorCode(MouseError::WriteFailed));
    }

    return {};
#endif
}

std::expected<std::size_t, std::error_code> Win32SerialPort::readSome(
    std::span<std::uint8_t> buffer) {
#if !defined(_WIN32)
    static_cast<void>(buffer);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::lock_guard<std::mutex> lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    DWORD bytesRead = 0;
    const BOOL readCompleted =
        ReadFile(serialHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead,
                 nullptr);
    if (readCompleted == FALSE) {
        return std::unexpected(makeErrorCode(MouseError::ReadFailed));
    }

    return static_cast<std::size_t>(bytesRead);
#endif
}

} // namespace vf
