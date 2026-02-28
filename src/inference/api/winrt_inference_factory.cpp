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

std::expected<WinrtInferenceBundle, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              InferenceResultStore& resultStore, IProfiler* profiler) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    try {
        auto sequencer = std::make_unique<FrameSequencer<InferenceFrame>>();
        auto dmlSession = std::make_unique<OnnxDmlSession>(inferenceConfig.modelPath);
        auto imageProcessor = std::make_unique<DmlImageProcessor>(*dmlSession, profiler);
        auto worker = std::make_unique<DmlInferenceWorker<InferenceFrame>>(
            sequencer.get(), dmlSession.get(), imageProcessor.get(), &resultStore, profiler);

        auto processor = std::make_unique<OnnxDmlInferenceProcessor>(
            inferenceConfig, std::move(sequencer), &resultStore, std::move(dmlSession),
            std::move(imageProcessor), std::move(worker), profiler);
        IWinrtFrameSink& frameSink = *processor;

        return WinrtInferenceBundle{
            .processor = std::move(processor),
            .frameSink = frameSink,
        };
    } catch (...) {
        return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
    }
#else
    auto processor = std::make_unique<DebugInferenceProcessor>();
    IWinrtFrameSink& frameSink = *processor;
    static_cast<void>(inferenceConfig);
    static_cast<void>(resultStore);
    static_cast<void>(profiler);
    return WinrtInferenceBundle{
        .processor = std::move(processor),
        .frameSink = frameSink,
    };
#endif
}

} // namespace vf
