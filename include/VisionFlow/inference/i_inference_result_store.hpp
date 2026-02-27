#pragma once

#include <optional>

#include "VisionFlow/inference/inference_result.hpp"

namespace vf {

class IInferenceResultStore {
  public:
    IInferenceResultStore() = default;
    IInferenceResultStore(const IInferenceResultStore&) = default;
    IInferenceResultStore(IInferenceResultStore&&) = default;
    IInferenceResultStore& operator=(const IInferenceResultStore&) = default;
    IInferenceResultStore& operator=(IInferenceResultStore&&) = default;
    virtual ~IInferenceResultStore() = default;

    virtual void publish(InferenceResult result) = 0;
    [[nodiscard]] virtual std::optional<InferenceResult> take() = 0;
};

} // namespace vf
