#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"
#include "VisionFlow/input/i_serial_port.hpp"

namespace vf {

class MakcuAckGate;
class MakcuCommandQueue;
class MakcuStateMachine;

class MakcuMouseController final : public IMouseController {
  public:
    MakcuMouseController(std::unique_ptr<ISerialPort> serialPort,
                         std::unique_ptr<IDeviceScanner> deviceScanner, MakcuConfig makcuConfig);
    MakcuMouseController(const MakcuMouseController&) = delete;
    MakcuMouseController(MakcuMouseController&&) = delete;
    MakcuMouseController& operator=(const MakcuMouseController&) = delete;
    MakcuMouseController& operator=(MakcuMouseController&&) = delete;
    ~MakcuMouseController() override;

    [[nodiscard]] std::expected<void, std::error_code> connect() override;
    [[nodiscard]] std::expected<void, std::error_code> disconnect() override;
    [[nodiscard]] std::expected<void, std::error_code> move(float dx, float dy) override;

  private:
    static constexpr const char* kTargetHardwareId = "VID_1A86&PID_55D3";
    static constexpr std::uint32_t kInitialBaudRate = 115200;
    static constexpr std::uint32_t kUpgradedBaudRate = 4000000;

    [[nodiscard]] std::expected<void, std::error_code> writeText(std::string_view text);
    [[nodiscard]] std::expected<void, std::error_code> runUpgradeHandshake();
    [[nodiscard]] std::expected<void, std::error_code> sendBaudChangeFrame(std::uint32_t baudRate);
    void onDataReceived(std::span<const std::uint8_t> payload);
    void handleSendError(const std::error_code& error);
    void stopSenderThread();
    void senderLoop(const std::stop_token& stopToken);

    std::unique_ptr<ISerialPort> serialPort;
    std::unique_ptr<IDeviceScanner> deviceScanner;
    MakcuConfig makcuConfig;

    std::unique_ptr<MakcuStateMachine> stateMachine;
    std::unique_ptr<MakcuCommandQueue> commandQueue;
    std::unique_ptr<MakcuAckGate> ackGate;
    std::jthread sendThread;
};

} // namespace vf
