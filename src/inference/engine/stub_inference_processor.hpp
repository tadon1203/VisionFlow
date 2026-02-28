#pragma once

#include <expected>
#include <system_error>

#include "VisionFlow/inference/i_inference_processor.hpp"

namespace vf {

class StubInferenceProcessor final : public IInferenceProcessor {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start() override;
    [[nodiscard]] std::expected<void, std::error_code> stop() override;
    [[nodiscard]] std::expected<void, std::error_code> poll() override;
};

} // namespace vf
