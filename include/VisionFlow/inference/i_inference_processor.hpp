#pragma once

#include <expected>
#include <system_error>

namespace vf {

class IInferenceProcessor {
  public:
    IInferenceProcessor() = default;
    IInferenceProcessor(const IInferenceProcessor&) = default;
    IInferenceProcessor(IInferenceProcessor&&) = default;
    IInferenceProcessor& operator=(const IInferenceProcessor&) = default;
    IInferenceProcessor& operator=(IInferenceProcessor&&) = default;
    virtual ~IInferenceProcessor() = default;

    [[nodiscard]] virtual std::expected<void, std::error_code> start() = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code> stop() = 0;
    [[nodiscard]] virtual std::expected<void, std::error_code> poll() = 0;
};

} // namespace vf
