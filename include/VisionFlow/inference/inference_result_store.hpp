#pragma once

#include <mutex>
#include <optional>

#include "VisionFlow/inference/inference_result.hpp"

namespace vf {

class InferenceResultStore final {
  public:
    void publish(InferenceResult result);
    [[nodiscard]] std::optional<InferenceResult> take();

  private:
    std::mutex mutex;
    std::optional<InferenceResult> latestResult;
};

} // namespace vf
