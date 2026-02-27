#pragma once

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/capture/i_capture_runtime.hpp"

namespace vf {

class CaptureRuntimeStateMachine;
class IInferenceProcessor;
class IWinrtFrameSink;
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
    [[nodiscard]] std::expected<void, std::error_code> poll() override;
    [[nodiscard]] std::expected<void, std::error_code>
    attachInferenceProcessor(IInferenceProcessor& processor);

  private:
    void attachFrameSink(IWinrtFrameSink* frameSink);

    IWinrtFrameSink* frameSink = nullptr;
    std::unique_ptr<WinrtCaptureSource> source;
    std::unique_ptr<CaptureRuntimeStateMachine> runtimeState;
};

} // namespace vf
