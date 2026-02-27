#pragma once

#include <expected>
#include <memory>
#include <system_error>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"

namespace vf {

class IInferenceProcessor;

[[nodiscard]] std::expected<std::unique_ptr<IInferenceProcessor>, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              IInferenceResultStore& resultStore);

} // namespace vf
