#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace vf {

struct AppConfig {
    std::chrono::milliseconds reconnectRetryMs{500};
};

struct MakcuConfig {
    std::chrono::milliseconds remainderTtlMs{200};
};

struct CaptureConfig {
    std::uint32_t preferredDisplayIndex{0};
};

struct InferenceConfig {
    std::string modelPath{"model.onnx"};
};

struct VisionFlowConfig {
    AppConfig app;
    MakcuConfig makcu;
    CaptureConfig capture;
    InferenceConfig inference;
};

} // namespace vf
