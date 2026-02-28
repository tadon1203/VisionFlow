#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <system_error>
#include <vector>

#include "VisionFlow/inference/inference_result.hpp"

namespace vf {

class InferencePostprocessor final {
  public:
    struct Settings {
        std::string outputTensorName{"output0"};
        std::array<int64_t, 3> outputTensorShape{1, 5, 8400};
        float confidenceThreshold = 0.25F;
        float nmsIouThreshold = 0.45F;
        std::size_t maxDetections = 100U;
        std::vector<std::int32_t> allowedClassIds{0};
    };

    InferencePostprocessor();
    explicit InferencePostprocessor(Settings settings);

    [[nodiscard]] std::expected<void, std::error_code> process(InferenceResult& result) const;

  private:
    [[nodiscard]] bool isClassAllowed(std::int32_t classId) const;

    Settings settings;
};

} // namespace vf
