#include "VisionFlow/input/makcu_mouse_controller.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_serial_port.hpp"
#include "VisionFlow/input/mouse_error.hpp"
#include "input/makcu/makcu_ack_gate.hpp"
#include "input/makcu/makcu_command_queue.hpp"
#include "input/makcu/makcu_controller_state.hpp"

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

MakcuMouseController::MakcuMouseController(std::unique_ptr<ISerialPort> serialPort,
                                           std::unique_ptr<IDeviceScanner> deviceScanner,
                                           MakcuConfig makcuConfig)
    : serialPort(std::move(serialPort)), deviceScanner(std::move(deviceScanner)),
      makcuConfig(makcuConfig), stateMachine(std::make_unique<MakcuStateMachine>()),
      commandQueue(std::make_unique<MakcuCommandQueue>()),
      ackGate(std::make_unique<MakcuAckGate>()) {}

MakcuMouseController::~MakcuMouseController() {
    const std::expected<void, std::error_code> result = disconnect();
    if (!result) {
        VF_ERROR("MakcuMouseController disconnect during destruction failed: {}",
                 result.error().message());
    }
}

void MakcuMouseController::stopSenderThread() {
    if (sendThread.joinable()) {
        sendThread.request_stop();
        commandQueue->wakeAll();
        ackGate->wakeAll();
        sendThread.join();
    }
}

std::expected<void, std::error_code> MakcuMouseController::connect() {
    const std::expected<void, std::error_code> beginConnectResult = stateMachine->beginConnect();
    if (!beginConnectResult) {
        VF_WARN("MakcuMouseController connect rejected: state transition in progress");
        return std::unexpected(beginConnectResult.error());
    }

    stopSenderThread();

    if (stateMachine->isReady()) {
        return {};
    }

    if (serialPort == nullptr || deviceScanner == nullptr) {
        stateMachine->setFault();
        VF_ERROR("MakcuMouseController connect failed: platform adapters are not available");
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    VF_DEBUG("MakcuMouseController scanning target hardware id: {}", kTargetHardwareId);
    const std::expected<std::string, std::error_code> portResult =
        deviceScanner->findPortByHardwareId(kTargetHardwareId);
    if (!portResult) {
        stateMachine->setIdle();
        VF_WARN("MakcuMouseController connect failed during device scan: {}",
                portResult.error().message());
        return std::unexpected(portResult.error());
    }

    VF_DEBUG("MakcuMouseController opening serial port: {} @ {}", portResult.value(),
             kInitialBaudRate);
    const std::expected<void, std::error_code> openResult =
        serialPort->open(portResult.value(), kInitialBaudRate);
    if (!openResult) {
        stateMachine->setIdle();
        VF_WARN("MakcuMouseController connect failed during serial open: {}",
                openResult.error().message());
        return std::unexpected(openResult.error());
    }

    const std::expected<void, std::error_code> handshakeResult = runUpgradeHandshake();
    if (!handshakeResult) {
        const std::expected<void, std::error_code> closeResult = serialPort->close();
        if (!closeResult) {
            VF_WARN("MakcuMouseController close after handshake failure failed: {}",
                    closeResult.error().message());
        }

        stateMachine->setIdle();
        VF_WARN("MakcuMouseController connect failed during handshake: {}",
                handshakeResult.error().message());
        return std::unexpected(handshakeResult.error());
    }

    commandQueue->reset();
    ackGate->reset();

    serialPort->setDataReceivedHandler(
        [this](std::span<const std::uint8_t> payload) { onDataReceived(payload); });

    sendThread = std::jthread([this](const std::stop_token& stopToken) { senderLoop(stopToken); });
    stateMachine->setReady();

    VF_INFO("MakcuMouseController connected: {}", portResult.value());
    return {};
}

std::expected<void, std::error_code> MakcuMouseController::disconnect() {
    const bool shouldDisconnect = stateMachine->beginDisconnect();
    if (!shouldDisconnect) {
        return {};
    }

    stopSenderThread();

    if (serialPort != nullptr) {
        serialPort->setDataReceivedHandler(nullptr);
    }

    commandQueue->reset();
    ackGate->reset();
    ackGate->wakeAll();

    const std::expected<void, std::error_code> closeResult =
        serialPort ? serialPort->close() : std::expected<void, std::error_code>{};
    stateMachine->setDisconnectResult(closeResult.has_value());
    return closeResult;
}

std::expected<void, std::error_code> MakcuMouseController::move(float dx, float dy) {
    if (!stateMachine->isReady()) {
        return std::unexpected(makeErrorCode(MouseError::NotConnected));
    }

    return commandQueue->enqueue(dx, dy, makcuConfig.remainderTtlMs);
}

std::expected<void, std::error_code> MakcuMouseController::writeText(std::string_view text) {
    if (serialPort == nullptr) {
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::span<const char> textBytes(text.data(), text.size());
    const std::span<const std::byte> bytes = std::as_bytes(textBytes);
    return serialPort->write({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

std::expected<void, std::error_code> MakcuMouseController::runUpgradeHandshake() {
    const std::expected<void, std::error_code> frameResult = sendBaudChangeFrame(kUpgradedBaudRate);
    if (!frameResult) {
        return std::unexpected(frameResult.error());
    }

    std::this_thread::sleep_for(kHandshakeStabilizationDelay);

    const std::expected<void, std::error_code> configureResult =
        serialPort->configure(kUpgradedBaudRate);
    if (!configureResult) {
        return std::unexpected(configureResult.error());
    }

    const std::expected<void, std::error_code> echoWriteResult = writeText(kEchoCommand);
    if (!echoWriteResult) {
        return std::unexpected(echoWriteResult.error());
    }

    return {};
}

std::expected<void, std::error_code>
MakcuMouseController::sendBaudChangeFrame(std::uint32_t baudRate) {
    if (serialPort == nullptr) {
        return std::unexpected(makeErrorCode(MouseError::PlatformNotSupported));
    }

    const std::array<std::uint8_t, 9> frame = buildBaudRateChangeFrame(baudRate);
    return serialPort->write(frame);
}

void MakcuMouseController::onDataReceived(std::span<const std::uint8_t> payload) {
    ackGate->onDataReceived(payload, kAckPrompt, kAckBufferLimit);
}

void MakcuMouseController::handleSendError(const std::error_code& error) {
    VF_ERROR("MakcuMouseController sender failure: {}", error.message());

    const std::expected<void, std::error_code> closeResult = serialPort->close();
    if (!closeResult) {
        VF_WARN("MakcuMouseController close after move send failure failed: {}",
                closeResult.error().message());
    }

    stateMachine->setIdle();
}

void MakcuMouseController::senderLoop(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        MakcuCommandQueue::MoveCommand command;
        if (!commandQueue->waitAndPop(stopToken, command)) {
            break;
        }
        if (!ackGate->waitUntilSendAllowed(stopToken)) {
            break;
        }

        const int clampedDx = std::clamp(command.dx, -kPerCommandClamp, kPerCommandClamp);
        const int clampedDy = std::clamp(command.dy, -kPerCommandClamp, kPerCommandClamp);
        const int overflowDx = command.dx - clampedDx;
        const int overflowDy = command.dy - clampedDy;
        command.dx = clampedDx;
        command.dy = clampedDy;

        if (overflowDx != 0 || overflowDy != 0) {
            commandQueue->requeue(overflowDx, overflowDy);
        }

        std::array<char, 64> commandBuffer{};
        const std::expected<std::size_t, std::error_code> commandSize =
            buildMoveCommand(command.dx, command.dy, commandBuffer);
        if (!commandSize) {
            VF_ERROR("MakcuMouseController move command format failed: {}",
                     commandSize.error().message());
            continue;
        }

        const std::span<const std::uint8_t> payload(
            reinterpret_cast<const std::uint8_t*>(commandBuffer.data()), commandSize.value());
        ackGate->markAckPending();
        const std::expected<void, std::error_code> writeResult = serialPort->write(payload);
        if (!writeResult) {
            ackGate->clearPendingAndAllowSend();
            handleSendError(writeResult.error());
            break;
        }
        if (!ackGate->waitForAck(stopToken, kAckTimeout)) {
            handleSendError(makeErrorCode(MouseError::ProtocolError));
            break;
        }
    }
}

} // namespace vf
