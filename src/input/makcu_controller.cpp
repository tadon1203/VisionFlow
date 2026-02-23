#include "VisionFlow/input/makcu_controller.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
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

constexpr auto kHandshakeStabilizationDelay = std::chrono::milliseconds(2);
constexpr std::string_view kEchoCommand = "km.echo(0)\r\n";
constexpr auto kAckTimeout = std::chrono::milliseconds(20);
constexpr std::string_view kAckPrompt = ">>> ";
constexpr std::size_t kAckBufferLimit = 1024;
constexpr int kPerCommandClamp = 127;

std::array<std::uint8_t, 9> buildBaudRateChangeFrame(std::uint32_t baudRate) {
    return {
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
}

} // namespace

MakcuController::MakcuController(std::unique_ptr<ISerialPort> serialPort,
                                 std::unique_ptr<IDeviceScanner> deviceScanner,
                                 MakcuConfig makcuConfig)
    : serialPort(std::move(serialPort)), deviceScanner(std::move(deviceScanner)),
      makcuConfig(makcuConfig) {}

MakcuController::~MakcuController() {
    const std::expected<void, std::error_code> result = disconnect();
    if (!result) {
        VF_ERROR("MakcuController disconnect during destruction failed: {}",
                 result.error().message());
    }
}

void MakcuController::stopSenderThread() {
    if (sendThread.joinable()) {
        sendThread.request_stop();
        commandCv.notify_all();
        sendThread.join();
    }
}

std::expected<void, std::error_code> MakcuController::connect() {
    {
        std::scoped_lock lock(stateMutex);
        if (state == ControllerState::Ready) {
            return {};
        }
        if (state == ControllerState::Opening || state == ControllerState::Stopping) {
            VF_WARN("MakcuController connect rejected: state transition in progress");
            return std::unexpected(makeErrorCode(MouseError::ProtocolError));
        }
        state = ControllerState::Opening;
    }
    VF_DEBUG("MakcuController connect requested");

    stopSenderThread();

    if (!serialPort || !deviceScanner) {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Fault;
        VF_ERROR("MakcuController connect failed: platform adapters are not available");
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    VF_DEBUG("MakcuController scanning target hardware id: {}", kTargetHardwareId);
    const std::expected<std::string, std::error_code> portResult =
        deviceScanner->findPortByHardwareId(kTargetHardwareId);
    if (!portResult) {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Idle;
        VF_WARN("MakcuController connect failed during device scan: {}",
                portResult.error().message());
        return std::unexpected(portResult.error());
    }

    VF_DEBUG("MakcuController opening serial port: {} @ {}", portResult.value(), kInitialBaudRate);
    const std::expected<void, std::error_code> openResult =
        serialPort->open(portResult.value(), kInitialBaudRate);
    if (!openResult) {
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Idle;
        VF_WARN("MakcuController connect failed during serial open: {}",
                openResult.error().message());
        return std::unexpected(openResult.error());
    }

    VF_DEBUG("MakcuController running baud upgrade handshake");
    const std::expected<void, std::error_code> handshakeResult = runUpgradeHandshake();
    if (!handshakeResult) {
        const std::expected<void, std::error_code> closeResult = serialPort->close();
        if (!closeResult) {
            VF_WARN("MakcuController close after handshake failure failed: {}",
                    closeResult.error().message());
        }
        std::scoped_lock lock(stateMutex);
        state = ControllerState::Idle;
        VF_WARN("MakcuController connect failed during handshake: {}",
                handshakeResult.error().message());
        return std::unexpected(handshakeResult.error());
    }

    {
        std::scoped_lock lock(commandMutex);
        pending = false;
        pendingCommand = {};
        remainder = {0.0F, 0.0F};
        lastInputTime = std::chrono::steady_clock::now();
    }
    {
        std::scoped_lock lock(ackMutex);
        sendAllowed = true;
        ackPending = false;
        ackBuffer.clear();
    }
    serialPort->setDataReceivedHandler(
        [this](std::span<const std::uint8_t> payload) { onDataReceived(payload); });

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

    stopSenderThread();

    if (serialPort) {
        serialPort->setDataReceivedHandler(nullptr);
    }

    {
        std::scoped_lock lock(commandMutex);
        pending = false;
        pendingCommand = {};
        remainder = {0.0F, 0.0F};
        lastInputTime = std::chrono::steady_clock::now();
    }
    {
        std::scoped_lock lock(ackMutex);
        sendAllowed = true;
        ackPending = false;
        ackBuffer.clear();
    }
    ackCv.notify_all();

    const std::expected<void, std::error_code> closeResult =
        serialPort ? serialPort->close() : std::expected<void, std::error_code>{};
    {
        std::scoped_lock lock(stateMutex);
        state = closeResult ? ControllerState::Idle : ControllerState::Fault;
    }

    return closeResult;
}

std::expected<void, std::error_code> MakcuController::move(float dx, float dy) {
    {
        std::scoped_lock lock(stateMutex);
        if (state != ControllerState::Ready) {
            return std::unexpected(makeErrorCode(MouseError::NotConnected));
        }
    }

    bool notifySender = false;
    {
        std::scoped_lock lock(commandMutex);
        const auto now = std::chrono::steady_clock::now();
        resetRemainderIfTtlExpired(now);
        const std::expected<void, std::error_code> accumulationResult =
            applyAccumulationAndQueue(dx, dy);
        if (!accumulationResult) {
            return std::unexpected(accumulationResult.error());
        }
        lastInputTime = now;
        notifySender = pending;
    }

    if (notifySender) {
        commandCv.notify_one();
    }
    return {};
}

std::expected<void, std::error_code> MakcuController::applyAccumulationAndQueue(float dx,
                                                                                float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return std::unexpected(makeErrorCode(MouseError::ProtocolError));
    }

    const float accumulatedX = remainder[0] + dx;
    const float accumulatedY = remainder[1] + dy;
    if (!std::isfinite(accumulatedX) || !std::isfinite(accumulatedY)) {
        return std::unexpected(makeErrorCode(MouseError::ProtocolError));
    }

    const double truncatedX = std::trunc(static_cast<double>(accumulatedX));
    const double truncatedY = std::trunc(static_cast<double>(accumulatedY));
    if (truncatedX > static_cast<double>(std::numeric_limits<int>::max()) ||
        truncatedX < static_cast<double>(std::numeric_limits<int>::min()) ||
        truncatedY > static_cast<double>(std::numeric_limits<int>::max()) ||
        truncatedY < static_cast<double>(std::numeric_limits<int>::min())) {
        return std::unexpected(makeErrorCode(MouseError::ProtocolError));
    }

    const int intPartX = static_cast<int>(truncatedX);
    const int intPartY = static_cast<int>(truncatedY);

    remainder[0] = accumulatedX - static_cast<float>(intPartX);
    remainder[1] = accumulatedY - static_cast<float>(intPartY);

    pendingCommand.dx += intPartX;
    pendingCommand.dy += intPartY;
    pending = pendingCommand.dx != 0 || pendingCommand.dy != 0;
    return {};
}

void MakcuController::resetRemainderIfTtlExpired(std::chrono::steady_clock::time_point now) {
    if (now - lastInputTime <= makcuConfig.remainderTtlMs) {
        return;
    }
    remainder = {0.0F, 0.0F};
}

void MakcuController::splitAndRequeueOverflow(MoveCommand& command) {
    const int clampedDx = std::clamp(command.dx, -kPerCommandClamp, kPerCommandClamp);
    const int clampedDy = std::clamp(command.dy, -kPerCommandClamp, kPerCommandClamp);
    const int overflowDx = command.dx - clampedDx;
    const int overflowDy = command.dy - clampedDy;

    command.dx = clampedDx;
    command.dy = clampedDy;

    if (overflowDx == 0 && overflowDy == 0) {
        return;
    }

    std::scoped_lock lock(commandMutex);
    pendingCommand.dx += overflowDx;
    pendingCommand.dy += overflowDy;
    pending = pendingCommand.dx != 0 || pendingCommand.dy != 0;
}

std::expected<void, std::error_code> MakcuController::writeText(std::string_view text) {
    if (!serialPort) {
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::span<const char> textBytes(text.data(), text.size());
    const std::span<const std::byte> bytes = std::as_bytes(textBytes);
    return serialPort->write({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

std::expected<void, std::error_code> MakcuController::runUpgradeHandshake() {
    VF_DEBUG("MakcuController handshake: send baud-change frame (target={})", kUpgradedBaudRate);
    const std::expected<void, std::error_code> frameResult = sendBaudChangeFrame(kUpgradedBaudRate);
    if (!frameResult) {
        VF_WARN("MakcuController handshake failed at frame send: {}",
                frameResult.error().message());
        return std::unexpected(frameResult.error());
    }

    std::this_thread::sleep_for(kHandshakeStabilizationDelay);

    VF_DEBUG("MakcuController handshake: reconfigure host baud to {}", kUpgradedBaudRate);
    const std::expected<void, std::error_code> configureResult =
        serialPort->configure(kUpgradedBaudRate);
    if (!configureResult) {
        VF_WARN("MakcuController handshake failed at host reconfigure: {}",
                configureResult.error().message());
        return std::unexpected(configureResult.error());
    }

    VF_DEBUG("MakcuController handshake: send echo command");
    const std::expected<void, std::error_code> echoWriteResult = writeText(kEchoCommand);
    if (!echoWriteResult) {
        VF_WARN("MakcuController handshake failed at echo write: {}",
                echoWriteResult.error().message());
        return std::unexpected(echoWriteResult.error());
    }

    VF_DEBUG("MakcuController handshake completed");
    return {};
}

std::expected<void, std::error_code> MakcuController::sendBaudChangeFrame(std::uint32_t baudRate) {
    if (!serialPort) {
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::array<std::uint8_t, 9> frame = buildBaudRateChangeFrame(baudRate);
    return serialPort->write(frame);
}

bool MakcuController::waitAndPopCommand(const std::stop_token& stopToken, MoveCommand& command) {
    std::unique_lock<std::mutex> lock(commandMutex);
    commandCv.wait(lock, [this, &stopToken] { return stopToken.stop_requested() || pending; });
    if (stopToken.stop_requested()) {
        return false;
    }

    command = pendingCommand;
    pendingCommand = {};
    pending = false;
    return true;
}

bool MakcuController::waitUntilSendAllowed(const std::stop_token& stopToken) {
    std::unique_lock<std::mutex> lock(ackMutex);
    ackCv.wait(lock, [this, &stopToken] { return stopToken.stop_requested() || sendAllowed; });
    return !stopToken.stop_requested();
}

void MakcuController::markAckPending() {
    std::scoped_lock lock(ackMutex);
    sendAllowed = false;
    ackPending = true;
}

bool MakcuController::waitForAck(const std::stop_token& stopToken) {
    std::unique_lock<std::mutex> lock(ackMutex);
    const bool ackReceived = ackCv.wait_for(lock, kAckTimeout, [this, &stopToken] {
        return stopToken.stop_requested() || !ackPending;
    });

    if (stopToken.stop_requested()) {
        return false;
    }
    if (!ackReceived || ackPending) {
        ackPending = false;
        sendAllowed = true;
        return false;
    }
    return true;
}

void MakcuController::onDataReceived(std::span<const std::uint8_t> payload) {
    std::scoped_lock lock(ackMutex);

    ackBuffer.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    consumeAckToken();
}

void MakcuController::consumeAckToken() {
    const std::size_t ackPosition = ackBuffer.find(kAckPrompt);
    if (ackPosition != std::string::npos) {
        if (ackPending) {
            ackPending = false;
            sendAllowed = true;
            ackCv.notify_one();
        }
        ackBuffer.erase(0, ackPosition + kAckPrompt.size());
        return;
    }

    if (ackBuffer.size() > kAckBufferLimit) {
        ackBuffer.erase(0, ackBuffer.size() - kAckBufferLimit);
    }
}

void MakcuController::handleSendError(const std::error_code& error) {
    VF_ERROR("MakcuController sender failure: {}", error.message());

    const std::expected<void, std::error_code> closeResult = serialPort->close();
    if (!closeResult) {
        VF_WARN("MakcuController close after move send failure failed: {}",
                closeResult.error().message());
    }

    std::scoped_lock lock(stateMutex);
    if (state == ControllerState::Ready) {
        state = ControllerState::Idle;
    }
}

void MakcuController::senderLoop(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        MoveCommand command;
        if (!waitAndPopCommand(stopToken, command)) {
            break;
        }
        if (!waitUntilSendAllowed(stopToken)) {
            break;
        }

        splitAndRequeueOverflow(command);

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
        markAckPending();
        const std::expected<void, std::error_code> writeResult = serialPort->write(payload);
        if (!writeResult) {
            {
                std::scoped_lock lock(ackMutex);
                ackPending = false;
                sendAllowed = true;
            }
            ackCv.notify_one();
            handleSendError(writeResult.error());
            break;
        }
        if (!waitForAck(stopToken)) {
            handleSendError(makeErrorCode(MouseError::ProtocolError));
            break;
        }
    }
}

} // namespace vf
