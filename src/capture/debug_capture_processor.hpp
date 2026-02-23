#pragma once

#include "capture/i_capture_processor.hpp"

namespace vf {

class DebugCaptureProcessor final : public ICaptureProcessor {
  public:
#ifdef _WIN32
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override;
#else
    void onFrame(void* texture, const CaptureFrameInfo& info) override;
#endif
};

} // namespace vf
