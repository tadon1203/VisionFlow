#include <array>
#include <cstdint>
#include <unordered_map>

#include <gtest/gtest.h>

#include "VisionFlow/core/config.hpp"

#if defined(_WIN32)
#include "input/platform/winrt_aim_activation_input.hpp"
#endif

namespace vf {
namespace {

#if defined(_WIN32)

TEST(AimActivationInputTest, ReturnsTrueWhenAllConfiguredButtonsArePressed) {
    AimConfig config;
    config.triggerThreshold = 0.5F;
    config.activationButtons = {{"Mouse:Right", "Key:Shift", "Pad:LT"}};

    const auto keyReader = [](int vk) {
        if (vk == VK_RBUTTON || vk == VK_SHIFT) {
            return static_cast<SHORT>(0x8000);
        }
        return static_cast<SHORT>(0);
    };

    const auto xinputReader = [](DWORD userIndex, XINPUT_STATE* state) {
        if (userIndex != 0) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        *state = {};
        state->Gamepad.bLeftTrigger = 200;
        return ERROR_SUCCESS;
    };

    const WinrtAimActivationInput input(config, keyReader, xinputReader);
    EXPECT_TRUE(input.isAimActivationPressed());
}

TEST(AimActivationInputTest, ReturnsFalseWhenAnyConfiguredButtonIsNotPressed) {
    AimConfig config;
    config.activationButtons = {{"Mouse:Right", "Key:Shift"}};

    const auto keyReader = [](int vk) {
        if (vk == VK_RBUTTON) {
            return static_cast<SHORT>(0x8000);
        }
        return static_cast<SHORT>(0);
    };

    const auto xinputReader = [](DWORD /*userIndex*/, XINPUT_STATE* /*state*/) {
        return ERROR_DEVICE_NOT_CONNECTED;
    };

    const WinrtAimActivationInput input(config, keyReader, xinputReader);
    EXPECT_FALSE(input.isAimActivationPressed());
}

TEST(AimActivationInputTest, ReturnsFalseWhenNoActivationButtonsConfigured) {
    AimConfig config;
    const auto keyReader = [](int /*vk*/) { return static_cast<SHORT>(0); };
    const auto xinputReader = [](DWORD /*userIndex*/, XINPUT_STATE* /*state*/) {
        return ERROR_DEVICE_NOT_CONNECTED;
    };

    const WinrtAimActivationInput input(config, keyReader, xinputReader);
    EXPECT_FALSE(input.isAimActivationPressed());
}

TEST(AimActivationInputTest, UsesAnyConnectedGamepadForPadTokens) {
    AimConfig config;
    config.activationButtons = {{"Pad:A"}};

    const auto keyReader = [](int /*vk*/) { return static_cast<SHORT>(0); };
    const auto xinputReader = [](DWORD userIndex, XINPUT_STATE* state) {
        if (userIndex == 1) {
            *state = {};
            state->Gamepad.wButtons = XINPUT_GAMEPAD_A;
            return ERROR_SUCCESS;
        }
        return ERROR_DEVICE_NOT_CONNECTED;
    };

    const WinrtAimActivationInput input(config, keyReader, xinputReader);
    EXPECT_TRUE(input.isAimActivationPressed());
}

#endif

} // namespace
} // namespace vf
