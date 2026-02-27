#pragma once

#include <expected>
#include <system_error>

namespace vf {

template <typename T>
[[nodiscard]] inline std::expected<void, std::error_code>
propagateFailure(const std::expected<T, std::error_code>& result) {
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

[[nodiscard]] inline std::expected<void, std::error_code>
pollFaultState(bool fault, const std::error_code& lastError, const std::error_code& fallbackError) {
    if (!fault) {
        return {};
    }
    if (lastError) {
        return std::unexpected(lastError);
    }
    return std::unexpected(fallbackError);
}

} // namespace vf
