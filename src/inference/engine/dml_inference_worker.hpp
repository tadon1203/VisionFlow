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
        if (!hasRuntimeComponents()) {
            reportMissingRuntimeComponent();
            return;
        }

        while (!stopToken.stop_requested()) {
            if (!processInFlightFrame()) {
                return;
            }

            TFrame frame;
            if (!frameSequencer->waitAndTakeLatest(stopToken, frame)) {
                continue;
            }
            if (!hasValidFrame(frame)) {
                reportMissingRuntimeComponent();
                return;
            }
            if (!processFrame(frame)) {
                return;
            }
        }
    }

  private:
    [[nodiscard]] bool hasRuntimeComponents() const {
        return frameSequencer != nullptr && session != nullptr && dmlImageProcessor != nullptr &&
               resultStore != nullptr;
    }

    [[nodiscard]] static bool hasValidFrame(const TFrame& frame) {
        return frame.texture != nullptr;
    }

    void reportFault(std::string_view reason, std::error_code errorCode) const {
        if (faultHandler) {
            faultHandler(reason, errorCode);
        }
    }

    void reportMissingRuntimeComponent() const {
        reportFault("OnnxDmlInferenceProcessor runtime component is missing",
                    makeErrorCode(InferenceError::InvalidState));
    }

    [[nodiscard]] bool processInFlightFrame() {
        if (!inFlightFrameTimestamp100ns.has_value()) {
            return true;
        }

        if (profiler != nullptr) {
            profiler->recordEvent(ProfileStage::InferenceCollect);
        }

        const auto collectStartedAt = std::chrono::steady_clock::now();
        const auto collectResult = dmlImageProcessor->tryCollectPreprocessResult();
        if (profiler != nullptr) {
            const auto collectEndedAt = std::chrono::steady_clock::now();
            profiler->recordCpuUs(
                ProfileStage::InferenceCollect,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               collectEndedAt - collectStartedAt)
                                               .count()));
        }

        if (!collectResult) {
            reportFault("OnnxDmlInferenceProcessor preprocess collect failed",
                        collectResult.error());
            return false;
        }

        if (!collectResult->has_value()) {
            if (profiler != nullptr) {
                profiler->recordEvent(ProfileStage::InferenceCollectMiss);
            }
            return true;
        }

        const DmlImageProcessor::DispatchResult dispatchResult = collectResult->value();
        const auto inferenceStartedAt = std::chrono::steady_clock::now();
        const auto inferenceResult =
            session->runWithGpuInput(*inFlightFrameTimestamp100ns, dispatchResult.outputResource,
                                     dispatchResult.outputBytes);
        if (profiler != nullptr) {
            const auto inferenceEndedAt = std::chrono::steady_clock::now();
            profiler->recordCpuUs(
                ProfileStage::InferenceRun,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               inferenceEndedAt - inferenceStartedAt)
                                               .count()));
        }

        if (!inferenceResult) {
            VF_WARN("OnnxDmlInferenceProcessor inference failed: {}",
                    inferenceResult.error().message());
        } else {
            resultStore->publish(inferenceResult.value());
        }
        inFlightFrameTimestamp100ns.reset();
        return true;
    }

    [[nodiscard]] bool processFrame(const TFrame& frame) {
        const auto initializeStartedAt = std::chrono::steady_clock::now();
        const auto initializeResult = dmlImageProcessor->initialize(frame.texture.get());
        if (profiler != nullptr) {
            const auto initializeEndedAt = std::chrono::steady_clock::now();
            profiler->recordCpuUs(
                ProfileStage::InferenceInitialize,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               initializeEndedAt - initializeStartedAt)
                                               .count()));
        }

        if (!initializeResult) {
            reportFault("OnnxDmlInferenceProcessor GPU initialization failed",
                        initializeResult.error());
            return false;
        }

        if (profiler != nullptr) {
            profiler->recordEvent(ProfileStage::InferenceEnqueue);
        }

        const auto enqueueStartedAt = std::chrono::steady_clock::now();
        const auto enqueueResult =
            dmlImageProcessor->enqueuePreprocess(frame.texture.get(), frame.fenceValue);
        if (profiler != nullptr) {
            const auto enqueueEndedAt = std::chrono::steady_clock::now();
            profiler->recordCpuUs(
                ProfileStage::InferenceEnqueue,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               enqueueEndedAt - enqueueStartedAt)
                                               .count()));
        }

        if (!enqueueResult) {
            reportFault("OnnxDmlInferenceProcessor preprocess enqueue failed",
                        enqueueResult.error());
            return false;
        }

        if (*enqueueResult == DmlImageProcessor::EnqueueStatus::Submitted) {
            inFlightFrameTimestamp100ns = frame.info.systemRelativeTime100ns;
        } else if (profiler != nullptr) {
            profiler->recordEvent(ProfileStage::InferenceEnqueueSkipped);
        }
        return true;
    }

    FrameSequencer<TFrame>* frameSequencer;
    OnnxDmlSession* session;
    DmlImageProcessor* dmlImageProcessor;
    InferenceResultStore* resultStore;
    IProfiler* profiler;
    FaultHandler faultHandler;
    std::optional<std::int64_t> inFlightFrameTimestamp100ns;
};

} // namespace vf
