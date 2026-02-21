#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>

#include "VisionFlow/input/i_serial_port.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

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
    [[nodiscard]] std::expected<std::size_t, std::error_code>
    readSome(std::span<std::uint8_t> buffer) override;

  private:
    [[nodiscard]] static std::string makeComPath(const std::string& portName);

    std::mutex handleMutex;
#ifdef _WIN32
    HANDLE serialHandle = INVALID_HANDLE_VALUE;
#endif
    bool opened = false;
};

} // namespace vf
