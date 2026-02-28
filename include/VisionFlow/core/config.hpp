#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

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
    float confidenceThreshold{0.25F};
};

struct AimConfig {
    float aimStrength{0.4F};
    std::int32_t aimMaxStep{127};
    float triggerThreshold{0.5F};
    std::vector<std::vector<std::string>> activationButtons{};
};

struct ProfilerConfig {
    bool enabled{false};
    std::chrono::milliseconds reportIntervalMs{1000};
};

struct VisionFlowConfig {
    AppConfig app;
    MakcuConfig makcu;
    CaptureConfig capture;
    InferenceConfig inference;
    AimConfig aim;
    ProfilerConfig profiler;
};

} // namespace vf
