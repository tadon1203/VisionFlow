#include "input/platform/winrt_aim_activation_input.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "input/string_utils.hpp"

namespace vf {

namespace {

[[nodiscard]] std::optional<int> parseKeyboardVk(std::string_view name) {
    static const std::unordered_map<std::string, int> kSpecialKeys = {
        {"SHIFT", VK_SHIFT}, {"CTRL", VK_CONTROL}, {"ALT", VK_MENU},     {"SPACE", VK_SPACE},
        {"TAB", VK_TAB},     {"ESC", VK_ESCAPE},   {"ENTER", VK_RETURN}, {"UP", VK_UP},
        {"DOWN", VK_DOWN},   {"LEFT", VK_LEFT},    {"RIGHT", VK_RIGHT},
    };

    if (name.size() == 1) {
        const char c = name.front();
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<int>(c);
        }
    }

    const auto it = kSpecialKeys.find(std::string(name));
    if (it != kSpecialKeys.end()) {
        return it->second;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<int> parseMouseVk(std::string_view name) {
    static const std::unordered_map<std::string, int> kMouseKeys = {
        {"LEFT", VK_LBUTTON}, {"RIGHT", VK_RBUTTON}, {"MIDDLE", VK_MBUTTON},
        {"X1", VK_XBUTTON1},  {"X2", VK_XBUTTON2},
    };

    const auto it = kMouseKeys.find(std::string(name));
    if (it != kMouseKeys.end()) {
        return it->second;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<WORD> parsePadMask(std::string_view name) {
    static const std::unordered_map<std::string, WORD> kPadButtons = {
        {"A", XINPUT_GAMEPAD_A},
        {"B", XINPUT_GAMEPAD_B},
        {"X", XINPUT_GAMEPAD_X},
        {"Y", XINPUT_GAMEPAD_Y},
        {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER},
        {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {"BACK", XINPUT_GAMEPAD_BACK},
        {"START", XINPUT_GAMEPAD_START},
        {"LTHUMB", XINPUT_GAMEPAD_LEFT_THUMB},
        {"RTHUMB", XINPUT_GAMEPAD_RIGHT_THUMB},
        {"DPADUP", XINPUT_GAMEPAD_DPAD_UP},
        {"DPADDOWN", XINPUT_GAMEPAD_DPAD_DOWN},
        {"DPADLEFT", XINPUT_GAMEPAD_DPAD_LEFT},
        {"DPADRIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
    };

    const auto it = kPadButtons.find(std::string(name));
    if (it != kPadButtons.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace

WinrtAimActivationInput::WinrtAimActivationInput(const AimConfig& config,
                                                 KeyStateReader keyStateReader,
                                                 XinputReader xinputReader)
    : triggerThreshold(config.triggerThreshold), bindings(buildBindings(config.activationButtons)),
      keyStateReader(std::move(keyStateReader)), xinputReader(std::move(xinputReader)) {
    if (!this->keyStateReader) {
        this->keyStateReader = [](int vk) { return ::GetAsyncKeyState(vk); };
    }
    if (!this->xinputReader) {
        this->xinputReader = [](DWORD userIndex, XINPUT_STATE* state) {
            return ::XInputGetState(userIndex, state);
        };
    }
}

bool WinrtAimActivationInput::isAimActivationPressed() const {
    if (bindings.empty()) {
        return false;
    }

    return std::ranges::all_of(
        bindings, [this](const ButtonBinding& binding) { return isBindingPressed(binding); });
}

std::vector<WinrtAimActivationInput::ButtonBinding> WinrtAimActivationInput::buildBindings(
    const std::vector<std::vector<std::string>>& activationButtons) {
    if (activationButtons.empty()) {
        return {};
    }

    const std::vector<std::string>& combo = activationButtons.front();
    std::vector<ButtonBinding> parsedBindings;
    parsedBindings.reserve(combo.size());
    for (const std::string& tokenRaw : combo) {
        const std::string token = input::detail::toUpper(tokenRaw);
        const auto separatorPos = token.find(':');
        if (separatorPos == std::string::npos || separatorPos == 0U ||
            separatorPos == (token.size() - 1U)) {
            continue;
        }

        const std::string prefix = token.substr(0, separatorPos);
        const std::string name = token.substr(separatorPos + 1U);
        if (prefix == "KEY") {
            const auto vk = parseKeyboardVk(name);
            if (!vk) {
                continue;
            }
            parsedBindings.emplace_back(ButtonBinding{
                .type = ButtonType::Keyboard,
                .vk = *vk,
            });
        } else if (prefix == "MOUSE") {
            const auto vk = parseMouseVk(name);
            if (!vk) {
                continue;
            }
            parsedBindings.emplace_back(ButtonBinding{
                .type = ButtonType::Mouse,
                .vk = *vk,
            });
        } else if (prefix == "PAD") {
            if (name == "LT") {
                parsedBindings.emplace_back(ButtonBinding{
                    .type = ButtonType::PadTrigger,
                    .leftTrigger = true,
                });
                continue;
            }
            if (name == "RT") {
                parsedBindings.emplace_back(ButtonBinding{
                    .type = ButtonType::PadTrigger,
                    .leftTrigger = false,
                });
                continue;
            }

            const auto mask = parsePadMask(name);
            if (!mask) {
                continue;
            }
            parsedBindings.emplace_back(ButtonBinding{
                .type = ButtonType::PadButton,
                .padMask = *mask,
            });
        }
    }

    return parsedBindings;
}

bool WinrtAimActivationInput::isBindingPressed(const ButtonBinding& binding) const {
    if (binding.type == ButtonType::Keyboard || binding.type == ButtonType::Mouse) {
        return (keyStateReader(binding.vk) & 0x8000) != 0;
    }

    for (DWORD userIndex = 0; userIndex < 4; ++userIndex) {
        XINPUT_STATE state{};
        if (xinputReader(userIndex, &state) != ERROR_SUCCESS) {
            continue;
        }

        if (binding.type == ButtonType::PadButton) {
            if ((state.Gamepad.wButtons & binding.padMask) != 0) {
                return true;
            }
            continue;
        }

        const float triggerValue = binding.leftTrigger
                                       ? static_cast<float>(state.Gamepad.bLeftTrigger) / 255.0F
                                       : static_cast<float>(state.Gamepad.bRightTrigger) / 255.0F;
        if (triggerValue >= triggerThreshold) {
            return true;
        }
    }

    return false;
}

} // namespace vf
