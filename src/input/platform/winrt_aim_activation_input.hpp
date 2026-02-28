#pragma once

#include <functional>
#include <string>
#include <vector>

#include <Windows.h>
#include <Xinput.h>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_aim_activation_input.hpp"

namespace vf {

class WinrtAimActivationInput final : public IAimActivationInput {
  public:
    using KeyStateReader = std::function<SHORT(int vk)>;
    using XinputReader = std::function<DWORD(DWORD userIndex, XINPUT_STATE* state)>;

    explicit WinrtAimActivationInput(const AimConfig& config, KeyStateReader keyStateReader = {},
                                     XinputReader xinputReader = {});

    [[nodiscard]] bool isAimActivationPressed() const override;

  private:
    enum class ButtonType : unsigned char {
        Keyboard,
        Mouse,
        PadButton,
        PadTrigger,
    };

    struct ButtonBinding {
        ButtonType type = ButtonType::Keyboard;
        int vk = 0;
        WORD padMask = 0;
        bool leftTrigger = false;
    };

    [[nodiscard]] static std::vector<ButtonBinding>
    buildBindings(const std::vector<std::vector<std::string>>& activationButtons);
    [[nodiscard]] bool isBindingPressed(const ButtonBinding& binding) const;

    float triggerThreshold;
    std::vector<ButtonBinding> bindings;
    KeyStateReader keyStateReader;
    XinputReader xinputReader;
};

} // namespace vf
