#pragma once

#include <expected>
#include <functional>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>

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
                       FaultHandler faultHandler = {})
        : frameSequencer(frameSequencer), session(session), dmlImageProcessor(dmlImageProcessor),
          resultStore(resultStore), faultHandler(std::move(faultHandler)) {}

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
                initializeResult = dmlImageProcessor->initialize(frame.texture.get());
            if (!initializeResult) {
                if (faultHandler) {
                    faultHandler("OnnxDmlInferenceProcessor GPU initialization failed",
                                 initializeResult.error());
                }
                return;
            }

            const std::expected<DmlImageProcessor::DispatchResult, std::error_code> dispatchResult =
                dmlImageProcessor->dispatch(frame.texture.get(), frame.fenceValue);
            if (!dispatchResult) {
                if (faultHandler) {
                    faultHandler("OnnxDmlInferenceProcessor preprocess failed",
                                 dispatchResult.error());
                }
                return;
            }

            const std::expected<InferenceResult, std::error_code> inferenceResult =
                session->runWithGpuInput(frame.info.systemRelativeTime100ns,
                                         dispatchResult->outputResource,
                                         dispatchResult->outputBytes);

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
    FaultHandler faultHandler;
};

} // namespace vf
