#pragma once

#include <memory>

#include "VisionFlow/core/config.hpp"

namespace vf {

class IInferenceProcessor;
class IWinrtFrameSink;

struct WinrtInferencePipeline {
    std::shared_ptr<IInferenceProcessor> processor;
    std::shared_ptr<IWinrtFrameSink> frameSink;
};

[[nodiscard]] WinrtInferencePipeline
createWinrtInferencePipeline(const InferenceConfig& inferenceConfig);

} // namespace vf
