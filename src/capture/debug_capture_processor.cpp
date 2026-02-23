#include "capture/debug_capture_processor.hpp"

#include "VisionFlow/core/logger.hpp"

namespace vf {

#ifdef _WIN32
void DebugCaptureProcessor::onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) {
#else
void DebugCaptureProcessor::onFrame(void* texture, const CaptureFrameInfo& info) {
#endif
    VF_DEBUG("Capture frame: texture={}, width={}, height={}, ts100ns={}",
             static_cast<const void*>(texture), info.width, info.height,
             info.systemRelativeTime100ns);
}

} // namespace vf
