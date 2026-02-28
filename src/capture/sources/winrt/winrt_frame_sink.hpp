#pragma once

#include <d3d11.h>

#include "capture/pipeline/capture_frame_info.hpp"

namespace vf {

class IWinrtFrameSink {
  public:
    IWinrtFrameSink() = default;
    IWinrtFrameSink(const IWinrtFrameSink&) = default;
    IWinrtFrameSink(IWinrtFrameSink&&) = default;
    IWinrtFrameSink& operator=(const IWinrtFrameSink&) = default;
    IWinrtFrameSink& operator=(IWinrtFrameSink&&) = default;
    virtual ~IWinrtFrameSink() = default;

    virtual void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) = 0;
};

} // namespace vf
