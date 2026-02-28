#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vf {

struct InferenceTensor {
    std::string name;
    std::vector<int64_t> shape;
    std::vector<float> values;
};

struct InferenceDetection {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float score = 0.0F;
    std::int32_t classId = 0;
};

struct InferenceResult {
    std::int64_t frameTimestamp100ns = 0;
    std::vector<InferenceTensor> tensors;
    std::vector<InferenceDetection> detections;
};

} // namespace vf
