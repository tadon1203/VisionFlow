#pragma once

#include <cstdint>

#include <d3d11.h>
#include <winrt/base.h>

#include "capture/common/capture_frame_info.hpp"

namespace vf {

struct WinrtCaptureFrame {
    CaptureFrameInfo info;
    winrt::com_ptr<ID3D11Texture2D> texture;
    std::uint64_t fenceValue = 0;
};

} // namespace vf
