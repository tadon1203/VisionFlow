#pragma once

#include <expected>
#include <system_error>

#include "VisionFlow/core/config.hpp"

namespace vf {

class ICaptureRuntime {
  public:
    ICaptureRuntime() = default;
    ICaptureRuntime(const ICaptureRuntime&) = default;
    ICaptureRuntime(ICaptureRuntime&&) = default;
    ICaptureRuntime& operator=(const ICaptureRuntime&) = default;
    ICaptureRuntime& operator=(ICaptureRuntime&&) = default;
    virtual ~ICaptureRuntime() = default;

    [[nodiscard]] virtual std::expected<void, std::error_code>
    start(const CaptureConfig& config) = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code> stop() = 0;
    // Poll must fail only when the runtime is in Fault or structurally invalid state.
    // Idle/Starting/Running/Stopping states are treated as healthy for polling.
    [[nodiscard]] virtual std::expected<void, std::error_code> poll() = 0;
};

} // namespace vf
