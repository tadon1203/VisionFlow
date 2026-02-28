#include "inference/composition/winrt_inference_factory.hpp"

#include <expected>
#include <memory>
#include <utility>

#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/inference_error.hpp"
#include "capture/pipeline/frame_sequencer.hpp"
#include "inference/backend/dml/dml_image_processor.hpp"
#include "inference/backend/dml/onnx_dml_session.hpp"
#include "inference/engine/dml_inference_worker.hpp"
#include "inference/engine/inference_frame.hpp"
#include "inference/engine/inference_postprocessor.hpp"
#include "inference/engine/onnx_dml_inference_processor.hpp"

namespace vf {

std::expected<WinrtInferenceBundle, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              InferenceResultStore& resultStore, IProfiler* profiler) {
#if !defined(VF_HAS_ONNXRUNTIME_DML) || !VF_HAS_ONNXRUNTIME_DML
    static_cast<void>(inferenceConfig);
    static_cast<void>(resultStore);
    static_cast<void>(profiler);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
#else
    try {
        auto sequencer = std::make_unique<FrameSequencer<InferenceFrame>>();
        auto dmlSession = std::make_unique<OnnxDmlSession>(inferenceConfig.modelPath);
        auto imageProcessor = std::make_unique<DmlImageProcessor>(*dmlSession, profiler);
        InferencePostprocessor::Settings postprocessorSettings;
        postprocessorSettings.confidenceThreshold = inferenceConfig.confidenceThreshold;
        auto postprocessor = std::make_unique<InferencePostprocessor>(postprocessorSettings);
        auto worker = std::make_unique<DmlInferenceWorker<InferenceFrame>>(
            sequencer.get(), dmlSession.get(), imageProcessor.get(), &resultStore,
            postprocessor.get(), profiler);

        auto processor = std::make_unique<OnnxDmlInferenceProcessor>(
            inferenceConfig, std::move(sequencer), &resultStore, std::move(dmlSession),
            std::move(imageProcessor), std::move(postprocessor), std::move(worker), profiler);
        IWinrtFrameSink& frameSink = *processor;

        return WinrtInferenceBundle{
            .processor = std::move(processor),
            .frameSink = frameSink,
        };
    } catch (...) {
        return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
    }
#endif
}

} // namespace vf
