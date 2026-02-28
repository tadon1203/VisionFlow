#pragma once

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/capture/i_capture_runtime.hpp"
#include "VisionFlow/core/i_profiler.hpp"

namespace vf {

class CaptureRuntimeStateMachine;
class IWinrtFrameSink;
class WinrtCaptureSource;

class WinrtCaptureRuntime final : public ICaptureRuntime {
  public:
    explicit WinrtCaptureRuntime(IProfiler* profiler = nullptr);
    WinrtCaptureRuntime(const WinrtCaptureRuntime&) = delete;
    WinrtCaptureRuntime(WinrtCaptureRuntime&&) = delete;
    WinrtCaptureRuntime& operator=(const WinrtCaptureRuntime&) = delete;
    WinrtCaptureRuntime& operator=(WinrtCaptureRuntime&&) = delete;
    ~WinrtCaptureRuntime() override;

    [[nodiscard]] std::expected<void, std::error_code> start(const CaptureConfig& config) override;
    [[nodiscard]] std::expected<void, std::error_code> stop() override;
    [[nodiscard]] std::expected<void, std::error_code> poll() override;
    [[nodiscard]] std::expected<void, std::error_code> attachFrameSink(IWinrtFrameSink& frameSink);

  private:
    void attachFrameSinkInternal(IWinrtFrameSink* frameSink);

    IWinrtFrameSink* frameSink = nullptr;
    std::unique_ptr<WinrtCaptureSource> source;
    std::unique_ptr<CaptureRuntimeStateMachine> runtimeState;
};

} // namespace vf
