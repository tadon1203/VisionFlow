#pragma once

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/capture/i_capture_runtime.hpp"

namespace vf {

class DebugCaptureProcessor;
class WinrtCaptureSource;

class WinrtCaptureRuntime final : public ICaptureRuntime {
  public:
    WinrtCaptureRuntime();
    WinrtCaptureRuntime(const WinrtCaptureRuntime&) = delete;
    WinrtCaptureRuntime(WinrtCaptureRuntime&&) = delete;
    WinrtCaptureRuntime& operator=(const WinrtCaptureRuntime&) = delete;
    WinrtCaptureRuntime& operator=(WinrtCaptureRuntime&&) = delete;
    ~WinrtCaptureRuntime() override;

    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config) override;
    [[nodiscard]] std::expected<void, std::error_code> stop() override;

  private:
    std::shared_ptr<DebugCaptureProcessor> processor;
    std::unique_ptr<WinrtCaptureSource> source;
};

} // namespace vf
