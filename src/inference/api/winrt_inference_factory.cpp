#include "inference/api/winrt_inference_factory.hpp"

#include <memory>

#include "VisionFlow/inference/i_inference_processor.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"
#include "inference/engine/debug_inference_processor.hpp"
#include "inference/engine/onnx_dml_inference_processor.hpp"

namespace vf {

WinrtInferencePipeline createWinrtInferencePipeline(const InferenceConfig& inferenceConfig) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    auto processor = std::make_shared<OnnxDmlCaptureProcessor>(inferenceConfig);
#else
    auto processor = std::make_shared<DebugCaptureProcessor>();
    static_cast<void>(inferenceConfig);
#endif

    std::shared_ptr<IInferenceProcessor> inferenceProcessor = processor;
    std::shared_ptr<IWinrtFrameSink> frameSink = processor;
    return {
        .processor = std::move(inferenceProcessor),
        .frameSink = std::move(frameSink),
    };
}

} // namespace vf
