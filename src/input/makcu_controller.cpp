#include "VisionFlow/input/makcu_controller.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_serial_port.hpp"
#include "VisionFlow/input/mouse_error.hpp"

namespace vf {

namespace {

std::expected<std::size_t, std::error_code> buildMoveCommand(int dx, int dy,
                                                             std::array<char, 64>& buffer) {
    static constexpr std::string_view kPrefix = "km.move(";
    static constexpr std::string_view kComma = ",";
    static constexpr std::string_view kSuffix = ")\r\n";

    std::size_t offset = 0;
    std::memcpy(buffer.data() + offset, kPrefix.data(), kPrefix.size());
    offset += kPrefix.size();

    auto resultDx = std::to_chars(buffer.data() + offset, buffer.data() + buffer.size(), dx);
    if (resultDx.ec != std::errc{}) {
        return std::unexpected(makeErrorCode(MouseError::WriteFailed));
    }
    offset = static_cast<std::size_t>(resultDx.ptr - buffer.data());

    std::memcpy(buffer.data() + offset, kComma.data(), kComma.size());
    offset += kComma.size();

    auto resultDy = std::to_chars(buffer.data() + offset, buffer.data() + buffer.size(), dy);
    if (resultDy.ec != std::errc{}) {
        return std::unexpected(makeErrorCode(MouseError::WriteFailed));
    }
    offset = static_cast<std::size_t>(resultDy.ptr - buffer.data());

    if (offset + kSuffix.size() > buffer.size()) {
        return std::unexpected(makeErrorCode(MouseError::WriteFailed));
    }
    std::memcpy(buffer.data() + offset, kSuffix.data(), kSuffix.size());
    offset += kSuffix.size();

    return offset;
}

} // namespace

MakcuController::MakcuController(std::unique_ptr<ISerialPort> serialPort,
                                 std::unique_ptr<IDeviceScanner> deviceScanner)
    : serialPort(std::move(serialPort)), deviceScanner(std::move(deviceScanner)) {}

MakcuController::~MakcuController() {
    const std::expected<void, std::error_code> result = disconnect();
    if (!result) {
        VF_ERROR("MakcuController disconnect during destruction failed: {}",
                 result.error().message());
    }
}

std::expected<void, std::error_code> MakcuController::connect() {
    {
        std::scoped_lock lock(stateMutex);
        if (state == ControllerState::Ready) {
            return {};
        }
        state = ControllerState::Opening;
    }

    if (!serialPort || !deviceScanner) {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Fault;
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::expected<std::string, std::error_code> portResult =
        deviceScanner->findPortByHardwareId(kTargetHardwareId);
    if (!portResult) {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Idle;
        return std::unexpected(portResult.error());
    }

    const std::expected<void, std::error_code> openResult =
        serialPort->open(portResult.value(), kInitialBaudRate);
    if (!openResult) {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Idle;
        return std::unexpected(openResult.error());
    }

    const std::expected<void, std::error_code> handshakeResult = runUpgradeHandshake();
    if (!handshakeResult) {
        const std::expected<void, std::error_code> closeResult = serialPort->close();
        if (!closeResult) {
            VF_WARN("MakcuController close after handshake failure failed: {}",
                    closeResult.error().message());
        }
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Idle;
        return std::unexpected(handshakeResult.error());
    }

    sendThread = std::jthread([this](const std::stop_token& st) { senderLoop(st); });
    {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Ready;
    }

    VF_INFO("MakcuController connected: {}", portResult.value());
    return {};
}

std::expected<void, std::error_code> MakcuController::disconnect() {
    {
        std::scoped_lock lock(stateMutex);
        if (state == ControllerState::Idle) {
            return {};
        }
        state = ControllerState::Stopping;
    }

    if (sendThread.joinable()) {
        sendThread.request_stop();
        commandCv.notify_all();
        sendThread.join();
    }

    {
        std::scoped_lock lock(commandMutex);
        pending = false;
        pendingCommand = {};
    }

    const std::expected<void, std::error_code> closeResult =
        serialPort ? serialPort->close() : std::expected<void, std::error_code>{};
    {
        std::scoped_lock lock(stateMutex);
        state = closeResult ? ControllerState::Idle : ControllerState::Fault;
    }

    return closeResult;
}

std::expected<void, std::error_code> MakcuController::move(int dx, int dy) {
    {
        std::scoped_lock lock(stateMutex);
        if (state != ControllerState::Ready) {
            return std::unexpected(makeErrorCode(MouseError::ThreadNotRunning));
        }
    }

    {
        std::scoped_lock lock(commandMutex);
        pendingCommand.dx = dx;
        pendingCommand.dy = dy;
        pending = true;
    }

    commandCv.notify_one();
    return {};
}

std::expected<void, std::error_code> MakcuController::writeText(const std::string& text) {
    if (!serialPort) {
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::span<const std::byte> bytes = std::as_bytes(std::span(text));
    return serialPort->write({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

std::expected<void, std::error_code> MakcuController::runUpgradeHandshake() {
    const std::expected<void, std::error_code> frameResult = sendBaudChangeFrame(kUpgradedBaudRate);
    if (!frameResult) {
        return std::unexpected(frameResult.error());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const std::expected<void, std::error_code> configureResult =
        serialPort->configure(kUpgradedBaudRate);
    if (!configureResult) {
        return std::unexpected(configureResult.error());
    }

    const std::expected<void, std::error_code> echoWriteResult = writeText("km.echo(0)\r\n");
    if (!echoWriteResult) {
        return std::unexpected(echoWriteResult.error());
    }

    return {};
}

std::expected<void, std::error_code> MakcuController::sendBaudChangeFrame(std::uint32_t baudRate) {
    if (!serialPort) {
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::array<std::uint8_t, 9> frame{
        0xDE,
        0xAD,
        0x05,
        0x00,
        0xA5,
        static_cast<std::uint8_t>(baudRate & 0xFFU),
        static_cast<std::uint8_t>((baudRate >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((baudRate >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((baudRate >> 24U) & 0xFFU),
    };

    return serialPort->write(frame);
}

void MakcuController::senderLoop(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        MoveCommand command;

        {
            std::unique_lock<std::mutex> lock(commandMutex);
            commandCv.wait(lock,
                           [this, &stopToken] { return stopToken.stop_requested() || pending; });

            if (stopToken.stop_requested()) {
                break;
            }

            command = pendingCommand;
            pending = false;
        }

        std::array<char, 64> commandBuffer{};
        const std::expected<std::size_t, std::error_code> commandSize =
            buildMoveCommand(command.dx, command.dy, commandBuffer);
        if (!commandSize) {
            VF_ERROR("MakcuController move command format failed: {}",
                     commandSize.error().message());
            continue;
        }

        const std::span<const std::uint8_t> payload(
            reinterpret_cast<const std::uint8_t*>(commandBuffer.data()), commandSize.value());
        const std::expected<void, std::error_code> writeResult = serialPort->write(payload);
        if (!writeResult) {
            VF_ERROR("MakcuController move send failed: {}", writeResult.error().message());
        }
    }
}

} // namespace vf
