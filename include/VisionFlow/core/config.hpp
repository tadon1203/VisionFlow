#pragma once

#include <chrono>

namespace vf {

struct AppConfig {
    std::chrono::milliseconds reconnectRetryMs{500};
};

struct MakcuConfig {
    std::chrono::milliseconds remainderTtlMs{200};
};

struct VisionFlowConfig {
    AppConfig app;
    MakcuConfig makcu;
};

} // namespace vf
