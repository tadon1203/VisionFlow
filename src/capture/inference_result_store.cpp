#include "capture/inference_result_store.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace vf {

void InferenceResultStore::publish(InferenceResult result) {
    std::scoped_lock lock(mutex);
    latestResult = std::move(result);
}

std::optional<InferenceResult> InferenceResultStore::latest() const {
    std::scoped_lock lock(mutex);
    return latestResult;
}

} // namespace vf
