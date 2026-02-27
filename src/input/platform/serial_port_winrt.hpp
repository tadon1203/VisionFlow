#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

#include "VisionFlow/input/i_serial_port.hpp"

#ifdef _WIN32
#include <winrt/Windows.Devices.SerialCommunication.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace vf {

class WinRtSerialPort final : public ISerialPort {
  public:
    WinRtSerialPort() = default;
    WinRtSerialPort(const WinRtSerialPort&) = delete;
    WinRtSerialPort(WinRtSerialPort&&) = delete;
    WinRtSerialPort& operator=(const WinRtSerialPort&) = delete;
    WinRtSerialPort& operator=(WinRtSerialPort&&) = delete;
    ~WinRtSerialPort() override;

    [[nodiscard]] std::expected<void, std::error_code> open(const std::string& portName,
                                                            std::uint32_t baudRate) override;
    [[nodiscard]] std::expected<void, std::error_code> close() override;
    [[nodiscard]] std::expected<void, std::error_code> configure(std::uint32_t baudRate) override;
    [[nodiscard]] std::expected<void, std::error_code> flush() override;
    [[nodiscard]] std::expected<void, std::error_code>
    write(std::span<const std::uint8_t> payload) override;
    void setDataReceivedHandler(DataReceivedHandler handler) override;
    [[nodiscard]] std::expected<std::size_t, std::error_code>
    readSome(std::span<std::uint8_t> buffer) override;

  private:
    void startReadThread();
    void stopReadThread();
    void readLoop(const std::stop_token& stopToken);

    std::mutex serialMutex;
    std::mutex callbackMutex;
    DataReceivedHandler dataReceivedHandler;

#ifdef _WIN32
    winrt::Windows::Devices::SerialCommunication::SerialDevice serialDevice{nullptr};
    winrt::Windows::Storage::Streams::DataReader dataReader{nullptr};
    winrt::Windows::Storage::Streams::DataWriter dataWriter{nullptr};
#endif

    bool opened = false;
    std::jthread readThread;
};

} // namespace vf
