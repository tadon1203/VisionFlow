#pragma once

#include <cstdint>
#include <expected>
#include <system_error>

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace vf {

struct CaptureFrameInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t systemRelativeTime100ns = 0;
};

class ICaptureProcessor {
  public:
    ICaptureProcessor() = default;
    ICaptureProcessor(const ICaptureProcessor&) = default;
    ICaptureProcessor(ICaptureProcessor&&) = default;
    ICaptureProcessor& operator=(const ICaptureProcessor&) = default;
    ICaptureProcessor& operator=(ICaptureProcessor&&) = default;
    virtual ~ICaptureProcessor() = default;

    [[nodiscard]] virtual std::expected<void, std::error_code> start() { return {}; }
    [[nodiscard]] virtual std::expected<void, std::error_code> stop() { return {}; }

#ifdef _WIN32
    virtual void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) = 0;
#else
    virtual void onFrame(void* texture, const CaptureFrameInfo& info) = 0;
#endif
};

} // namespace vf
