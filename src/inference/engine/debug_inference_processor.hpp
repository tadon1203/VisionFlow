#pragma once

#include <expected>
#include <system_error>

#include "VisionFlow/inference/i_inference_processor.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

namespace vf {

class DebugInferenceProcessor final : public IInferenceProcessor, public IWinrtFrameSink {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> stop() override { return {}; }
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override;
};

} // namespace vf
