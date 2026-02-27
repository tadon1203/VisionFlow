#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
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
            if (inFlightFrameTimestamp100ns.has_value()) {
                if (profiler != nullptr) {
                    profiler->recordEvent(ProfileStage::InferenceCollect);
                }
                const auto collectResult = [this]() {
                    const auto startedAt = std::chrono::steady_clock::now();
                    const auto result = dmlImageProcessor->tryCollectPreprocessResult();
                    if (profiler != nullptr) {
                        const auto endedAt = std::chrono::steady_clock::now();
                        profiler->recordCpuUs(
                            ProfileStage::InferenceCollect,
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(endedAt -
                                                                                      startedAt)
                                    .count()));
                    }
                    return result;
                }();
                if (!collectResult) {
                    if (faultHandler) {
                        faultHandler("OnnxDmlInferenceProcessor preprocess collect failed",
                                     collectResult.error());
                    }
                    return;
                }
                if (collectResult->has_value()) {
                    const auto inferenceResult = [this, &collectResult]() {
                        const auto startedAt = std::chrono::steady_clock::now();
                        const auto result = session->runWithGpuInput(
                            *inFlightFrameTimestamp100ns, collectResult->value().outputResource,
                            collectResult->value().outputBytes);
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
                    } else {
                        resultStore->publish(inferenceResult.value());
                    }
                    inFlightFrameTimestamp100ns.reset();
                } else if (profiler != nullptr) {
                    profiler->recordEvent(ProfileStage::InferenceCollectMiss);
                }
            }

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

            const auto initializeResult = [this, &frame]() {
                const auto startedAt = std::chrono::steady_clock::now();
                const auto result = dmlImageProcessor->initialize(frame.texture.get());
                if (profiler != nullptr) {
                    const auto endedAt = std::chrono::steady_clock::now();
                    profiler->recordCpuUs(ProfileStage::InferenceInitialize,
                                          static_cast<std::uint64_t>(
                                              std::chrono::duration_cast<std::chrono::microseconds>(
                                                  endedAt - startedAt)
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

            const auto enqueueResult = [this, &frame]() {
                if (profiler != nullptr) {
                    profiler->recordEvent(ProfileStage::InferenceEnqueue);
                }
                const auto startedAt = std::chrono::steady_clock::now();
                const auto result =
                    dmlImageProcessor->enqueuePreprocess(frame.texture.get(), frame.fenceValue);
                if (profiler != nullptr) {
                    const auto endedAt = std::chrono::steady_clock::now();
                    profiler->recordCpuUs(ProfileStage::InferenceEnqueue,
                                          static_cast<std::uint64_t>(
                                              std::chrono::duration_cast<std::chrono::microseconds>(
                                                  endedAt - startedAt)
                                                  .count()));
                }
                return result;
            }();
            if (!enqueueResult) {
                if (faultHandler) {
                    faultHandler("OnnxDmlInferenceProcessor preprocess enqueue failed",
                                 enqueueResult.error());
                }
                return;
            }

            if (*enqueueResult == DmlImageProcessor::EnqueueStatus::Submitted) {
                inFlightFrameTimestamp100ns = frame.info.systemRelativeTime100ns;
            } else if (profiler != nullptr) {
                profiler->recordEvent(ProfileStage::InferenceEnqueueSkipped);
            }
        }
    }

  private:
    FrameSequencer<TFrame>* frameSequencer;
    OnnxDmlSession* session;
    DmlImageProcessor* dmlImageProcessor;
    InferenceResultStore* resultStore;
    IProfiler* profiler;
    FaultHandler faultHandler;
    std::optional<std::int64_t> inFlightFrameTimestamp100ns;
};

} // namespace vf
