#include "VisionFlow/input/aim_activation_input_factory.hpp"

#include <memory>

#include "input/platform/aim_activation_input_stub.hpp"

#if defined(_WIN32)
#include "input/platform/winrt_aim_activation_input.hpp"
#endif

namespace vf {

std::unique_ptr<IAimActivationInput> createAimActivationInput(const VisionFlowConfig& config) {
#if defined(_WIN32)
    return std::make_unique<WinrtAimActivationInput>(config.aim);
#else
    static_cast<void>(config);
    return std::make_unique<AimActivationInputStub>();
#endif
}

} // namespace vf
