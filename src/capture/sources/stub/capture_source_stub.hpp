#pragma once

#include <expected>
#include <system_error>

#include "VisionFlow/capture/i_capture_source.hpp"

namespace vf {

class StubCaptureSource final : public ICaptureSource {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config) override;
    [[nodiscard]] std::expected<void, std::error_code> stop() override;
    [[nodiscard]] std::expected<void, std::error_code> poll() override;
};

} // namespace vf
