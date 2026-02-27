#pragma once

#include <cstdint>

namespace vf {

struct CaptureFrameInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t systemRelativeTime100ns = 0;
};

} // namespace vf
