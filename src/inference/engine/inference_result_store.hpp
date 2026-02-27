#pragma once

#include <mutex>
#include <optional>

#include "capture/common/inference_result.hpp"

namespace vf {

class InferenceResultStore {
  public:
    void publish(InferenceResult result);
    [[nodiscard]] std::optional<InferenceResult> latest() const;

  private:
    mutable std::mutex mutex;
    std::optional<InferenceResult> latestResult;
};

} // namespace vf
