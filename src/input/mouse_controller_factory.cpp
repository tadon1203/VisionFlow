#include "VisionFlow/input/mouse_controller_factory.hpp"

#include <memory>

#include "VisionFlow/input/makcu_controller.hpp"
#include "input/winrt_device_scanner.hpp"
#include "input/winrt_serial_port.hpp"

namespace vf {

std::unique_ptr<IMouseController> createMouseController(const VisionFlowConfig& config) {
    auto serialPort = std::make_unique<WinRtSerialPort>();
    auto deviceScanner = std::make_unique<WinRtDeviceScanner>();
    return std::make_unique<MakcuController>(std::move(serialPort), std::move(deviceScanner),
                                             config.makcu);
}

} // namespace vf
