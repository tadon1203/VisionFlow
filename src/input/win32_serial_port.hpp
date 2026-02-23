#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

#include <Windows.h>

#include "VisionFlow/input/i_serial_port.hpp"

namespace vf {

class Win32SerialPort final : public ISerialPort {
  public:
    Win32SerialPort() = default;
    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort(Win32SerialPort&&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(Win32SerialPort&&) = delete;
    ~Win32SerialPort() override;

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
    [[nodiscard]] static std::string makeComPath(const std::string& portName);
    void startReadThread();
    void stopReadThread();
    void readLoop(const std::stop_token& stopToken);

    std::mutex handleMutex;
    std::mutex callbackMutex;
    DataReceivedHandler dataReceivedHandler;
#ifdef _WIN32
    HANDLE serialHandle = INVALID_HANDLE_VALUE;
#endif
    bool opened = false;
    std::jthread readThread;
};

} // namespace vf
