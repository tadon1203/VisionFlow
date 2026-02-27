#include "inference/api/winrt_inference_factory.hpp"

#include <memory>

#include "VisionFlow/inference/i_inference_processor.hpp"
#include "inference/engine/debug_inference_processor.hpp"
#include "inference/engine/onnx_dml_inference_processor.hpp"

namespace vf {

std::expected<std::unique_ptr<IInferenceProcessor>, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              IInferenceResultStore& resultStore) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    auto processorResult = OnnxDmlCaptureProcessor::createDefault(inferenceConfig, &resultStore);
    if (!processorResult) {
        return std::unexpected(processorResult.error());
    }
    return std::move(processorResult.value());
#else
    std::unique_ptr<IInferenceProcessor> processor = std::make_unique<DebugCaptureProcessor>();
    static_cast<void>(inferenceConfig);
    static_cast<void>(resultStore);
    return std::move(processor);
#endif
}

} // namespace vf
