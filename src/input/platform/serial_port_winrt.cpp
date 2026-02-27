#include "input/platform/serial_port_winrt.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/input/mouse_error.hpp"

#ifdef _WIN32
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>
#endif

namespace vf {

namespace {

constexpr auto kWriteStoreTimeout = std::chrono::milliseconds(250);

void invokeDataHandlerSafely(const ISerialPort::DataReceivedHandler& handler,
                             std::span<const std::uint8_t> payload) {
    try {
        handler(payload);
#ifdef _WIN32
    } catch (const winrt::hresult_error& error) {
        VF_ERROR("WinRT exception in read handler: {} (0x{:08X})",
                 winrt::to_string(error.message()), static_cast<std::uint32_t>(error.code()));
#endif
    } catch (const std::exception& error) {
        VF_ERROR("Standard exception in read handler: {}", error.what());
    } catch (...) {
        VF_ERROR("Unknown exception in read handler");
    }
}

} // namespace

WinRtSerialPort::~WinRtSerialPort() {
    const std::expected<void, std::error_code> result = close();
    static_cast<void>(result);
}

std::expected<void, std::error_code> WinRtSerialPort::open(const std::string& portName,
                                                           std::uint32_t baudRate) {
#ifndef _WIN32
    static_cast<void>(portName);
    static_cast<void>(baudRate);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    namespace wde = winrt::Windows::Devices::Enumeration;
    namespace wds = winrt::Windows::Devices::SerialCommunication;
    namespace wss = winrt::Windows::Storage::Streams;

    {
        std::scoped_lock lock(serialMutex);
        if (opened) {
            return {};
        }

        try {
            const auto selector = wds::SerialDevice::GetDeviceSelector(winrt::to_hstring(portName));
            VF_DEBUG("WinRtSerialPort::open step: before FindAllAsync.get (port={})", portName);
            const auto devices = wde::DeviceInformation::FindAllAsync(selector).get();
            VF_DEBUG("WinRtSerialPort::open step: after FindAllAsync.get (count={})",
                     devices.Size());
            if (devices.Size() == 0) {
                return std::unexpected(makeErrorCode(MouseError::PortNotFound));
            }

            VF_DEBUG("WinRtSerialPort::open step: before FromIdAsync.get (port={})", portName);
            serialDevice = wds::SerialDevice::FromIdAsync(devices.GetAt(0).Id()).get();
            VF_DEBUG("WinRtSerialPort::open step: after FromIdAsync.get (hasDevice={})",
                     serialDevice ? 1 : 0);
            if (!serialDevice) {
                return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
            }

            serialDevice.ReadTimeout(std::chrono::milliseconds(1));
            serialDevice.WriteTimeout(std::chrono::milliseconds(50));
            serialDevice.Parity(wds::SerialParity::None);
            serialDevice.StopBits(wds::SerialStopBitCount::One);
            serialDevice.DataBits(8);
            serialDevice.Handshake(wds::SerialHandshake::None);

            dataReader = wss::DataReader(serialDevice.InputStream());
            dataReader.InputStreamOptions(wss::InputStreamOptions::Partial);
            dataWriter = wss::DataWriter(serialDevice.OutputStream());

            opened = true;
        } catch (const winrt::hresult_error&) {
            serialDevice = nullptr;
            dataReader = nullptr;
            dataWriter = nullptr;
            opened = false;
            return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
        }
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

std::expected<void, std::error_code> WinRtSerialPort::close() {
#ifndef _WIN32
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    stopReadThread();

    std::scoped_lock lock(serialMutex);
    if (!opened) {
        return {};
    }

    dataReader = nullptr;
    dataWriter = nullptr;
    serialDevice = nullptr;
    opened = false;

    return {};
#endif
}

std::expected<void, std::error_code> WinRtSerialPort::configure(std::uint32_t baudRate) {
#ifndef _WIN32
    static_cast<void>(baudRate);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(serialMutex);

    if (!opened || !serialDevice) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    try {
        serialDevice.BaudRate(baudRate);
        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(MouseError::ConfigureDcbFailed));
    }
#endif
}

std::expected<void, std::error_code> WinRtSerialPort::flush() {
#ifndef _WIN32
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(serialMutex);
    if (!opened || !dataReader || !dataWriter) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    try {
        dataReader.DetachStream();
        dataReader = winrt::Windows::Storage::Streams::DataReader(serialDevice.InputStream());
        dataReader.InputStreamOptions(
            winrt::Windows::Storage::Streams::InputStreamOptions::Partial);

        dataWriter.DetachStream();
        dataWriter = winrt::Windows::Storage::Streams::DataWriter(serialDevice.OutputStream());
        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(MouseError::ReadFailed));
    }
#endif
}

std::expected<void, std::error_code> WinRtSerialPort::write(std::span<const std::uint8_t> payload) {
#ifndef _WIN32
    static_cast<void>(payload);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(serialMutex);
    if (!opened || !dataWriter) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    try {
        dataWriter.WriteBytes(
            winrt::array_view<const std::uint8_t>(payload.data(), payload.data() + payload.size()));
        auto storeOperation = dataWriter.StoreAsync();
        const auto status = storeOperation.wait_for(kWriteStoreTimeout);
        if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
            storeOperation.Cancel();
            return std::unexpected(makeErrorCode(MouseError::WriteFailed));
        }

        const std::uint32_t written = storeOperation.GetResults();
        if (written != payload.size()) {
            return std::unexpected(makeErrorCode(MouseError::WriteFailed));
        }
        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(MouseError::WriteFailed));
    }
#endif
}

void WinRtSerialPort::setDataReceivedHandler(DataReceivedHandler handler) {
    std::scoped_lock lock(callbackMutex);
    dataReceivedHandler = std::move(handler);
}

std::expected<std::size_t, std::error_code>
WinRtSerialPort::readSome(std::span<std::uint8_t> buffer) {
#ifndef _WIN32
    static_cast<void>(buffer);
    return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
#else
    std::scoped_lock lock(serialMutex);
    if (!opened || !dataReader) {
        return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
    }

    try {
        const std::uint32_t bytesReceived = serialDevice ? serialDevice.BytesReceived() : 0U;
        if (bytesReceived == 0U) {
            return static_cast<std::size_t>(0);
        }

        const std::uint32_t bytesToRead =
            std::min(bytesReceived, static_cast<std::uint32_t>(buffer.size()));
        const std::uint32_t bytesRead = dataReader.LoadAsync(bytesToRead).get();
        for (std::uint32_t i = 0; i < bytesRead; ++i) {
            buffer[i] = dataReader.ReadByte();
        }
        return static_cast<std::size_t>(bytesRead);
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(MouseError::ReadFailed));
    }
#endif
}

void WinRtSerialPort::startReadThread() {
    if (readThread.joinable()) {
        return;
    }

    readThread = std::jthread([this](const std::stop_token& stopToken) { readLoop(stopToken); });
}

void WinRtSerialPort::stopReadThread() {
    if (!readThread.joinable()) {
        return;
    }

    readThread.request_stop();
    readThread.join();
}

void WinRtSerialPort::readLoop(const std::stop_token& stopToken) {
#ifdef _WIN32
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
        invokeDataHandlerSafely(handler, payload);
    }
#else
    static_cast<void>(stopToken);
#endif
}

} // namespace vf
