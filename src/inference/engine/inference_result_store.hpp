#pragma once

#include <mutex>
#include <optional>

#include "VisionFlow/inference/i_inference_result_store.hpp"

namespace vf {

class InferenceResultStore final : public IInferenceResultStore {
  public:
    void publish(InferenceResult result) override;
    [[nodiscard]] std::optional<InferenceResult> take() override;

  private:
    std::mutex mutex;
    std::optional<InferenceResult> latestResult;
};

} // namespace vf
