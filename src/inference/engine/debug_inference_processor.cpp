#include "inference/engine/debug_inference_processor.hpp"

#include "VisionFlow/core/logger.hpp"

namespace vf {

void DebugInferenceProcessor::onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) {
    VF_DEBUG("Capture frame: texture={}, width={}, height={}, ts100ns={}",
             static_cast<const void*>(texture), info.width, info.height,
             info.systemRelativeTime100ns);
}

} // namespace vf
