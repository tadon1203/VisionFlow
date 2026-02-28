#pragma once

#include <cstdint>

#ifdef _WIN32
#include <d3d11.h>
#include <winrt/base.h>
#endif

#include "capture/pipeline/capture_frame_info.hpp"

namespace vf {

struct InferenceFrame {
    CaptureFrameInfo info;
#ifdef _WIN32
    winrt::com_ptr<ID3D11Texture2D> texture;
#else
    void* texture = nullptr;
#endif
    std::uint64_t fenceValue = 0;
};

} // namespace vf
