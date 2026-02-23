#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace vf::input::detail {

[[nodiscard]] inline std::string toUpper(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return text;
}

} // namespace vf::input::detail
