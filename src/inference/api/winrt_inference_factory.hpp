#pragma once

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/core/i_profiler.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"

namespace vf {

class IInferenceProcessor;

[[nodiscard]] std::expected<std::unique_ptr<IInferenceProcessor>, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              InferenceResultStore& resultStore,
                              std::shared_ptr<IProfiler> profiler = nullptr);

} // namespace vf
