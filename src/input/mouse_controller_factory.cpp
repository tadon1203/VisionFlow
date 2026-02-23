#include "VisionFlow/input/mouse_controller_factory.hpp"

#include <memory>

#include "VisionFlow/input/makcu_controller.hpp"
#include "input/win32_device_scanner.hpp"
#include "input/win32_serial_port.hpp"

namespace vf {

std::unique_ptr<IMouseController> createMouseController(const VisionFlowConfig& config) {
    auto serialPort = std::make_unique<Win32SerialPort>();
    auto deviceScanner = std::make_unique<Win32DeviceScanner>();
    return std::make_unique<MakcuController>(std::move(serialPort), std::move(deviceScanner),
                                             config.makcu);
}

} // namespace vf
