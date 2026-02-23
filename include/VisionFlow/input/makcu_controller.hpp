#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"
#include "VisionFlow/input/i_serial_port.hpp"

namespace vf {

class MakcuController final : public IMouseController {
  public:
    MakcuController(std::unique_ptr<ISerialPort> serialPort,
                    std::unique_ptr<IDeviceScanner> deviceScanner, MakcuConfig makcuConfig);
    MakcuController(const MakcuController&) = delete;
    MakcuController(MakcuController&&) = delete;
    MakcuController& operator=(const MakcuController&) = delete;
    MakcuController& operator=(MakcuController&&) = delete;
    ~MakcuController() override;

    [[nodiscard]] std::expected<void, std::error_code> connect() override;
    [[nodiscard]] State getState() const override;
    [[nodiscard]] std::expected<void, std::error_code> disconnect() override;
    [[nodiscard]] std::expected<void, std::error_code> move(float dx, float dy) override;

  private:
    static constexpr const char* kTargetHardwareId = "VID_1A86&PID_55D3";
    static constexpr std::uint32_t kInitialBaudRate = 115200;
    static constexpr std::uint32_t kUpgradedBaudRate = 4000000;

    enum class ControllerState : std::uint8_t {
        Idle,
        Opening,
        Ready,
        Stopping,
        Fault,
    };

    struct MoveCommand {
        int dx = 0;
        int dy = 0;
    };

    [[nodiscard]] std::expected<void, std::error_code> writeText(std::string_view text);
    [[nodiscard]] std::expected<void, std::error_code> runUpgradeHandshake();
    [[nodiscard]] std::expected<void, std::error_code> sendBaudChangeFrame(std::uint32_t baudRate);
    [[nodiscard]] std::expected<void, std::error_code> applyAccumulationAndQueue(float dx,
                                                                                 float dy);
    void resetRemainderIfTtlExpired(std::chrono::steady_clock::time_point now);
    void splitAndRequeueOverflow(MoveCommand& command);
    [[nodiscard]] bool waitAndPopCommand(const std::stop_token& stopToken, MoveCommand& command);
    [[nodiscard]] bool waitUntilSendAllowed(const std::stop_token& stopToken);
    [[nodiscard]] bool waitForAck(const std::stop_token& stopToken);
    void markAckPending();
    void onDataReceived(std::span<const std::uint8_t> payload);
    void consumeAckToken();
    void handleSendError(const std::error_code& error);
    void stopSenderThread();

    void senderLoop(const std::stop_token& stopToken);

    std::unique_ptr<ISerialPort> serialPort;
    std::unique_ptr<IDeviceScanner> deviceScanner;
    MakcuConfig makcuConfig;

    mutable std::mutex stateMutex;
    ControllerState state = ControllerState::Idle;

    std::jthread sendThread;
    std::condition_variable commandCv;
    std::mutex commandMutex;
    bool pending = false;
    MoveCommand pendingCommand;
    std::array<float, 2> remainder{0.0F, 0.0F};
    std::chrono::steady_clock::time_point lastInputTime = std::chrono::steady_clock::now();

    std::condition_variable ackCv;
    std::mutex ackMutex;
    bool sendAllowed = true;
    bool ackPending = false;
    std::string ackBuffer;
};

} // namespace vf
