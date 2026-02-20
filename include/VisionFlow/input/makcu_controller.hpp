#pragma once

#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"
#include "VisionFlow/input/i_serial_port.hpp"

namespace vf {

class MakcuController final : public IMouseController {
  public:
    MakcuController();
    MakcuController(std::unique_ptr<ISerialPort> serialPort,
                    std::unique_ptr<IDeviceScanner> deviceScanner);
    MakcuController(const MakcuController&) = delete;
    MakcuController(MakcuController&&) = delete;
    MakcuController& operator=(const MakcuController&) = delete;
    MakcuController& operator=(MakcuController&&) = delete;
    ~MakcuController() override;

    [[nodiscard]] std::expected<void, std::error_code> connect() override;
    [[nodiscard]] std::expected<void, std::error_code> disconnect() override;
    [[nodiscard]] std::expected<void, std::error_code> move(int dx, int dy) override;

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

    [[nodiscard]] std::expected<void, std::error_code> writeText(const std::string& text);
    [[nodiscard]] std::expected<void, std::error_code> runUpgradeHandshake();
    [[nodiscard]] std::expected<void, std::error_code> sendBaudChangeFrame(
        std::uint32_t baudRate);

    void senderLoop(const std::stop_token& stopToken);

    std::unique_ptr<ISerialPort> serialPort;
    std::unique_ptr<IDeviceScanner> deviceScanner;

    std::mutex stateMutex;
    ControllerState state = ControllerState::Idle;

    std::jthread sendThread;
    std::condition_variable commandCv;
    std::mutex commandMutex;
    bool pending = false;
    MoveCommand pendingCommand;
};

} // namespace vf
