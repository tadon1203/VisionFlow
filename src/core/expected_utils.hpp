#pragma once

#include <expected>
#include <system_error>

namespace vf {

struct FaultPollErrors {
    std::error_code lastError;
    std::error_code fallbackError;
};

template <typename T>
[[nodiscard]] inline std::expected<void, std::error_code>
propagateFailure(const std::expected<T, std::error_code>& result) {
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

[[nodiscard]] inline std::expected<void, std::error_code>
pollFaultState(bool fault, const FaultPollErrors& errors) {
    if (!fault) {
        return {};
    }
    if (errors.lastError) {
        return std::unexpected(errors.lastError);
    }
    return std::unexpected(errors.fallbackError);
}

} // namespace vf
