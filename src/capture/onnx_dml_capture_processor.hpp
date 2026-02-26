#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>

#include "VisionFlow/core/config.hpp"
#include "capture/dml_image_processor.hpp"
#include "capture/frame_sequencer.hpp"
#include "capture/inference_result_store.hpp"
#include "capture/winrt/i_winrt_frame_sink.hpp"

namespace vf {

class OnnxDmlSession;

class OnnxDmlCaptureProcessor final : public IWinrtFrameSink {
  public:
    explicit OnnxDmlCaptureProcessor(InferenceConfig config);
    OnnxDmlCaptureProcessor(const OnnxDmlCaptureProcessor&) = delete;
    OnnxDmlCaptureProcessor(OnnxDmlCaptureProcessor&&) = delete;
    OnnxDmlCaptureProcessor& operator=(const OnnxDmlCaptureProcessor&) = delete;
    OnnxDmlCaptureProcessor& operator=(OnnxDmlCaptureProcessor&&) = delete;
    ~OnnxDmlCaptureProcessor() noexcept override;

    [[nodiscard]] std::expected<void, std::error_code> start() override;
    [[nodiscard]] std::expected<void, std::error_code> stop() override;

    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override;

  private:
    enum class ProcessorState : std::uint8_t {
        Idle,
        Starting,
        Running,
        Stopping,
        Fault,
    };

    void transitionToFault(std::string_view reason, std::error_code errorCode);
    void inferenceLoop(const std::stop_token& stopToken);

    InferenceConfig config;
    std::mutex stateMutex;
    ProcessorState state = ProcessorState::Idle;

    std::jthread workerThread;
    FrameSequencer frameSequencer;
    std::atomic<std::uint64_t> frameSequence{0};

    std::unique_ptr<OnnxDmlSession> session;
    std::unique_ptr<DmlImageProcessor> dmlImageProcessor;

    InferenceResultStore resultStore;
};

} // namespace vf
