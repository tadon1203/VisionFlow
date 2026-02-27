#include "VisionFlow/input/mouse_controller_factory.hpp"

#include <memory>

#include "VisionFlow/input/makcu_mouse_controller.hpp"
#include "input/platform/device_scanner_winrt.hpp"
#include "input/platform/serial_port_winrt.hpp"

namespace vf {

std::unique_ptr<IMouseController> createMouseController(const VisionFlowConfig& config) {
    auto serialPort = std::make_unique<WinrtSerialPort>();
    auto deviceScanner = std::make_unique<WinrtDeviceScanner>();
    return std::make_unique<MakcuMouseController>(std::move(serialPort), std::move(deviceScanner),
                                                  config.makcu);
}

} // namespace vf
