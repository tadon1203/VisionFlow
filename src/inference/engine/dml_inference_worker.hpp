#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>

#include "VisionFlow/core/i_profiler.hpp"
#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/inference_error.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"
#include "capture/pipeline/frame_sequencer.hpp"
#include "inference/platform/dml/dml_image_processor.hpp"
#include "inference/platform/dml/onnx_dml_session.hpp"

namespace vf {

template <typename TFrame> class DmlInferenceWorker {
  public:
    using FaultHandler = std::function<void(std::string_view reason, std::error_code errorCode)>;

    DmlInferenceWorker(FrameSequencer<TFrame>* frameSequencer, OnnxDmlSession* session,
                       DmlImageProcessor* dmlImageProcessor, InferenceResultStore* resultStore,
                       IProfiler* profiler = nullptr, FaultHandler faultHandler = {})
        : frameSequencer(frameSequencer), session(session), dmlImageProcessor(dmlImageProcessor),
          resultStore(resultStore), profiler(profiler), faultHandler(std::move(faultHandler)) {}

    void setFaultHandler(FaultHandler nextFaultHandler) {
        faultHandler = std::move(nextFaultHandler);
    }

    void run(const std::stop_token& stopToken) {
        if (frameSequencer == nullptr || session == nullptr || dmlImageProcessor == nullptr ||
            resultStore == nullptr) {
            if (faultHandler) {
                faultHandler("OnnxDmlInferenceProcessor runtime component is missing",
                             makeErrorCode(InferenceError::InvalidState));
            }
            return;
        }

        while (!stopToken.stop_requested()) {
            TFrame frame;
            if (!frameSequencer->waitAndTakeLatest(stopToken, frame)) {
                continue;
            }

            if (frame.texture == nullptr) {
                if (faultHandler) {
                    faultHandler("OnnxDmlInferenceProcessor runtime component is missing",
                                 makeErrorCode(InferenceError::InvalidState));
                }
                return;
            }

            const std::expected<DmlImageProcessor::InitializeResult, std::error_code>
                initializeResult = [this, &frame]() {
                    const auto startedAt = std::chrono::steady_clock::now();
                    const auto result = dmlImageProcessor->initialize(frame.texture.get());
                    if (profiler != nullptr) {
                        const auto endedAt = std::chrono::steady_clock::now();
                        profiler->recordCpuUs(
                            ProfileStage::InferenceInitialize,
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(endedAt -
                                                                                      startedAt)
                                    .count()));
                    }
                    return result;
                }();
            if (!initializeResult) {
                if (faultHandler) {
                    faultHandler("OnnxDmlInferenceProcessor GPU initialization failed",
                                 initializeResult.error());
                }
                return;
            }

            const std::expected<DmlImageProcessor::DispatchResult, std::error_code> dispatchResult =
                [this, &frame]() {
                    const auto startedAt = std::chrono::steady_clock::now();
                    const auto result =
                        dmlImageProcessor->dispatch(frame.texture.get(), frame.fenceValue);
                    if (profiler != nullptr) {
                        const auto endedAt = std::chrono::steady_clock::now();
                        profiler->recordCpuUs(
                            ProfileStage::InferencePreprocess,
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(endedAt -
                                                                                      startedAt)
                                    .count()));
                    }
                    return result;
                }();
            if (!dispatchResult) {
                if (faultHandler) {
                    faultHandler("OnnxDmlInferenceProcessor preprocess failed",
                                 dispatchResult.error());
                }
                return;
            }

            const std::expected<InferenceResult, std::error_code> inferenceResult =
                [this, &frame, &dispatchResult]() {
                    const auto startedAt = std::chrono::steady_clock::now();
                    const auto result = session->runWithGpuInput(frame.info.systemRelativeTime100ns,
                                                                 dispatchResult->outputResource,
                                                                 dispatchResult->outputBytes);
                    if (profiler != nullptr) {
                        const auto endedAt = std::chrono::steady_clock::now();
                        profiler->recordCpuUs(
                            ProfileStage::InferenceRun,
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(endedAt -
                                                                                      startedAt)
                                    .count()));
                    }
                    return result;
                }();

            if (!inferenceResult) {
                VF_WARN("OnnxDmlInferenceProcessor inference failed: {}",
                        inferenceResult.error().message());
                continue;
            }

            resultStore->publish(inferenceResult.value());
        }
    }

  private:
    FrameSequencer<TFrame>* frameSequencer;
    OnnxDmlSession* session;
    DmlImageProcessor* dmlImageProcessor;
    InferenceResultStore* resultStore;
    IProfiler* profiler;
    FaultHandler faultHandler;
};

} // namespace vf
