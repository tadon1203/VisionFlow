#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <system_error>

#include "VisionFlow/core/config.hpp"
#include "VisionFlow/core/i_profiler.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

namespace vf {

class IInferenceProcessor;

struct WinrtInferenceBundle {
    std::unique_ptr<IInferenceProcessor> processor;
    std::reference_wrapper<IWinrtFrameSink> frameSink;
};

[[nodiscard]] std::expected<WinrtInferenceBundle, std::error_code>
createWinrtInferenceProcessor(const InferenceConfig& inferenceConfig,
                              InferenceResultStore& resultStore, IProfiler* profiler = nullptr);

} // namespace vf
