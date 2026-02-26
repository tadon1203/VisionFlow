#pragma once

#include "capture/winrt/i_winrt_frame_sink.hpp"

namespace vf {

class DebugCaptureProcessor final : public IWinrtFrameSink {
  public:
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override;
};

} // namespace vf
