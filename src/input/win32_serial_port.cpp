#include "input/win32_serial_port.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/input/mouse_error.hpp"

namespace vf {

Win32SerialPort::~Win32SerialPort() {
    const std::expected<void, std::error_code> result = close();
    static_cast<void>(result);
}

std::string Win32SerialPort::makeComPath(const std::string& portName) {
    return R"(\\.\)" + portName;
}

std::expected<void, std::error_code> Win32SerialPort::open(const std::string& portName,
                                                           std::uint32_t baudRate) {
#ifndef _WIN32
    static_cast<void>(portName);
    static_cast<void>(baudRate);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(handleMutex);

        if (opened) {
            return {};
        }

        serialHandle = CreateFileA(makeComPath(portName).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (serialHandle == INVALID_HANDLE_VALUE) {
            return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
        }

        COMMTIMEOUTS timeouts{};
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

    startReadThread();
    return {};
#endif
}

std::expected<void, std::error_code> Win32SerialPort::close() {
#ifndef _WIN32
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    stopReadThread();
    std::scoped_lock lock(handleMutex);

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
#ifndef _WIN32
    static_cast<void>(baudRate);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    DCB dcb{};
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
#ifndef _WIN32
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    if (PurgeComm(serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR) == FALSE) {
        return std::unexpected(makeErrorCode(MouseError::ReadFailed));
    }

    return {};
#endif
}

std::expected<void, std::error_code> Win32SerialPort::write(std::span<const std::uint8_t> payload) {
#ifndef _WIN32
    static_cast<void>(payload);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    DWORD bytesWritten = 0;
    const BOOL writeCompleted = WriteFile(
        serialHandle, payload.data(), static_cast<DWORD>(payload.size()), &bytesWritten, nullptr);
    if (writeCompleted == FALSE || static_cast<std::size_t>(bytesWritten) != payload.size()) {
        return std::unexpected(makeErrorCode(MouseError::WriteFailed));
    }

    return {};
#endif
}

void Win32SerialPort::setDataReceivedHandler(DataReceivedHandler handler) {
    std::scoped_lock lock(callbackMutex);
    dataReceivedHandler = std::move(handler);
}

std::expected<std::size_t, std::error_code>
Win32SerialPort::readSome(std::span<std::uint8_t> buffer) {
#ifndef _WIN32
    static_cast<void>(buffer);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(handleMutex);

    if (!opened || serialHandle == INVALID_HANDLE_VALUE) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    DWORD bytesRead = 0;
    const BOOL readCompleted = ReadFile(serialHandle, buffer.data(),
                                        static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
    if (readCompleted == FALSE) {
        return std::unexpected(makeErrorCode(MouseError::ReadFailed));
    }

    return static_cast<std::size_t>(bytesRead);
#endif
}

void Win32SerialPort::startReadThread() {
    if (readThread.joinable()) {
        return;
    }
    readThread = std::jthread([this](const std::stop_token& stopToken) { readLoop(stopToken); });
}

void Win32SerialPort::stopReadThread() {
    if (!readThread.joinable()) {
        return;
    }
    readThread.request_stop();
    readThread.join();
}

void Win32SerialPort::readLoop(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        std::array<std::uint8_t, 256> buffer{};
        const std::expected<std::size_t, std::error_code> readResult = readSome(buffer);
        if (!readResult) {
            if (stopToken.stop_requested()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (readResult.value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        DataReceivedHandler handler;
        {
            std::scoped_lock lock(callbackMutex);
            handler = dataReceivedHandler;
        }
        if (!handler) {
            continue;
        }

        const std::span<const std::uint8_t> payload(buffer.data(), readResult.value());
        handler(payload);
    }
}

} // namespace vf
