#include "inference/engine/dml_inference_worker.hpp"

#include <expected>
#include <stop_token>
#include <system_error>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "inference/platform/dml/onnx_dml_session.hpp"

namespace vf {

DmlInferenceWorker::DmlInferenceWorker(FrameSequencer& frameSequencer, OnnxDmlSession& session,
                                       DmlImageProcessor& dmlImageProcessor,
                                       InferenceResultStore& resultStore, FaultHandler faultHandler)
    : frameSequencer(frameSequencer), session(session), dmlImageProcessor(dmlImageProcessor),
      resultStore(resultStore), faultHandler(std::move(faultHandler)) {}

void DmlInferenceWorker::run(const std::stop_token& stopToken) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    while (!stopToken.stop_requested()) {
        FrameSequencer::PendingFrame frame;
        if (!frameSequencer.waitAndTakeLatest(stopToken, frame)) {
            continue;
        }

        if (frame.texture == nullptr) {
            faultHandler("OnnxDmlCaptureProcessor runtime component is missing",
                         makeErrorCode(CaptureError::InvalidState));
            return;
        }

        const std::expected<DmlImageProcessor::InitializeResult, std::error_code> initializeResult =
            dmlImageProcessor.initialize(frame.texture.get());
        if (!initializeResult) {
            faultHandler("OnnxDmlCaptureProcessor GPU initialization failed",
                         initializeResult.error());
            return;
        }

        const std::expected<DmlImageProcessor::DispatchResult, std::error_code> dispatchResult =
            dmlImageProcessor.dispatch(frame.texture.get(), frame.fenceValue);
        if (!dispatchResult) {
            faultHandler("OnnxDmlCaptureProcessor preprocess failed", dispatchResult.error());
            return;
        }

        const std::expected<InferenceResult, std::error_code> inferenceResult =
            session.runWithGpuInput(frame.info.systemRelativeTime100ns,
                                    dispatchResult->outputResource, dispatchResult->outputBytes);

        if (!inferenceResult) {
            VF_WARN("OnnxDmlCaptureProcessor inference failed: {}",
                    inferenceResult.error().message());
            continue;
        }

        resultStore.publish(inferenceResult.value());
    }
#else
    static_cast<void>(stopToken);
#endif
}

} // namespace vf
