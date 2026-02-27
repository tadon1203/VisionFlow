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

struct InferenceResult {
    std::int64_t frameTimestamp100ns = 0;
    std::vector<InferenceTensor> tensors;
};

} // namespace vf
