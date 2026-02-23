#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <system_error>

namespace vf {

class ISerialPort {
  public:
    using DataReceivedHandler = std::function<void(std::span<const std::uint8_t> payload)>;

    ISerialPort() = default;
    ISerialPort(const ISerialPort&) = default;
    ISerialPort(ISerialPort&&) = default;
    ISerialPort& operator=(const ISerialPort&) = default;
    ISerialPort& operator=(ISerialPort&&) = default;
    virtual ~ISerialPort() = default;

    [[nodiscard]] virtual std::expected<void, std::error_code> open(const std::string& portName,
                                                                    std::uint32_t baudRate) = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code> close() = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code>
    configure(std::uint32_t baudRate) = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code> flush() = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code>
    write(std::span<const std::uint8_t> payload) = 0;
    virtual void setDataReceivedHandler(DataReceivedHandler handler) = 0;
    [[nodiscard]] virtual std::expected<std::size_t, std::error_code>
    readSome(std::span<std::uint8_t> buffer) = 0;
};

} // namespace vf
