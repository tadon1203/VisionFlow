#pragma once

#include <expected>
#include <system_error>

#include <d3d11.h>

#include "capture/common/capture_frame_info.hpp"

namespace vf {

class IWinrtFrameSink {
  public:
    IWinrtFrameSink() = default;
    IWinrtFrameSink(const IWinrtFrameSink&) = default;
    IWinrtFrameSink(IWinrtFrameSink&&) = default;
    IWinrtFrameSink& operator=(const IWinrtFrameSink&) = default;
    IWinrtFrameSink& operator=(IWinrtFrameSink&&) = default;
    virtual ~IWinrtFrameSink() = default;

    [[nodiscard]] virtual std::expected<void, std::error_code> start() { return {}; }
    [[nodiscard]] virtual std::expected<void, std::error_code> stop() { return {}; }

    virtual void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) = 0;
};

} // namespace vf
