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
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"
#include "capture/pipeline/frame_sequencer.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"
#include "inference/common/inference_frame.hpp"
#include "inference/engine/dml_inference_worker.hpp"
#include "inference/platform/dml/dml_image_processor.hpp"

namespace vf {

class OnnxDmlSession;

class OnnxDmlInferenceProcessor final : public IInferenceProcessor, public IWinrtFrameSink {
  public:
    static std::expected<std::unique_ptr<OnnxDmlInferenceProcessor>, std::error_code>
    createDefault(InferenceConfig config, IInferenceResultStore* resultStore);

    OnnxDmlInferenceProcessor(InferenceConfig config,
                              std::unique_ptr<FrameSequencer<InferenceFrame>> frameSequencer,
                              IInferenceResultStore* resultStore,
                              std::unique_ptr<OnnxDmlSession> session,
                              std::unique_ptr<DmlImageProcessor> dmlImageProcessor,
                              std::unique_ptr<DmlInferenceWorker<InferenceFrame>> inferenceWorker);
    OnnxDmlInferenceProcessor(const OnnxDmlInferenceProcessor&) = delete;
    OnnxDmlInferenceProcessor(OnnxDmlInferenceProcessor&&) = delete;
    OnnxDmlInferenceProcessor& operator=(const OnnxDmlInferenceProcessor&) = delete;
    OnnxDmlInferenceProcessor& operator=(OnnxDmlInferenceProcessor&&) = delete;
    ~OnnxDmlInferenceProcessor() noexcept override;

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

    std::unique_ptr<FrameSequencer<InferenceFrame>> frameSequencer;
    std::atomic<std::uint64_t> frameSequence{0};

    IInferenceResultStore* resultStore = nullptr;
    std::unique_ptr<OnnxDmlSession> session;
    std::unique_ptr<DmlImageProcessor> dmlImageProcessor;
    std::unique_ptr<DmlInferenceWorker<InferenceFrame>> inferenceWorker;
    std::jthread workerThread;
};

} // namespace vf
