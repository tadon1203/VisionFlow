#include "inference/api/winrt_inference_factory.hpp"

#include <expected>
#include <memory>
#include <utility>

#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/inference_error.hpp"
#include "capture/pipeline/frame_sequencer.hpp"
#include "inference/common/inference_frame.hpp"
#include "inference/engine/debug_inference_processor.hpp"
#include "inference/engine/dml_inference_worker.hpp"
#include "inference/engine/onnx_dml_inference_processor.hpp"
#include "inference/platform/dml/dml_image_processor.hpp"
#include "inference/platform/dml/onnx_dml_session.hpp"

namespace vf {

std::expected<std::unique_ptr<IInferenceProcessor>, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              InferenceResultStore& resultStore,
                              std::shared_ptr<IProfiler> profiler) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    try {
        auto sequencer = std::make_unique<FrameSequencer<InferenceFrame>>();
        auto dmlSession = std::make_unique<OnnxDmlSession>(inferenceConfig.modelPath);
        auto imageProcessor = std::make_unique<DmlImageProcessor>(*dmlSession, profiler);
        auto worker = std::make_unique<DmlInferenceWorker<InferenceFrame>>(
            sequencer.get(), dmlSession.get(), imageProcessor.get(), &resultStore, profiler);

        std::unique_ptr<IInferenceProcessor> processor =
            std::make_unique<OnnxDmlInferenceProcessor>(
                inferenceConfig, std::move(sequencer), &resultStore, std::move(dmlSession),
                std::move(imageProcessor), std::move(worker), std::move(profiler));

        return processor;
    } catch (...) {
        return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
    }
#else
    std::unique_ptr<IInferenceProcessor> processor = std::make_unique<DebugInferenceProcessor>();
    static_cast<void>(inferenceConfig);
    static_cast<void>(resultStore);
    static_cast<void>(profiler);
    return std::move(processor);
#endif
}

} // namespace vf
