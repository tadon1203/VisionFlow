#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "VisionFlow/core/config.hpp"

namespace vf {

namespace detail {

constexpr int kJsonTypeErrorId = 302;
constexpr int kJsonOutOfRangeId = 403;
constexpr int kJsonOtherErrorId = 501;

[[nodiscard]] inline std::chrono::milliseconds
readPositiveMilliseconds(const nlohmann::json& source, const char* key) {
    const nlohmann::json& value = source.at(key);
    if (!value.is_number_integer()) {
        throw nlohmann::json::type_error::create(
            kJsonTypeErrorId, std::string("expected integer for key '") + key + "'", &value);
    }

    constexpr auto maxRep = std::numeric_limits<std::chrono::milliseconds::rep>::max();
    if (value.is_number_unsigned()) {
        const auto raw = value.get<unsigned long long>();
        if (raw == 0ULL || raw > static_cast<unsigned long long>(maxRep)) {
            throw nlohmann::json::other_error::create(
                kJsonOtherErrorId, std::string("out of range for key '") + key + "'", &value);
        }
        return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(raw));
    }

    const auto raw = value.get<long long>();
    if (raw <= 0 || raw > static_cast<long long>(maxRep)) {
        throw nlohmann::json::other_error::create(
            kJsonOtherErrorId, std::string("out of range for key '") + key + "'", &value);
    }
    return std::chrono::milliseconds(raw);
}

} // namespace detail

// nlohmann::json customization points require these exact function names.
// NOLINTBEGIN(readability-identifier-naming)
inline void to_json(nlohmann::json& json, const AppConfig& config) {
    json = {{"reconnectRetryMs", config.reconnectRetryMs.count()}};
}

inline void from_json(const nlohmann::json& json, AppConfig& config) {
    config.reconnectRetryMs = detail::readPositiveMilliseconds(json, "reconnectRetryMs");
}

inline void to_json(nlohmann::json& json, const MakcuConfig& config) {
    json = {{"remainderTtlMs", config.remainderTtlMs.count()}};
}

inline void from_json(const nlohmann::json& json, MakcuConfig& config) {
    config.remainderTtlMs = detail::readPositiveMilliseconds(json, "remainderTtlMs");
}

inline void to_json(nlohmann::json& json, const CaptureConfig& config) {
    json = {{"preferredDisplayIndex", config.preferredDisplayIndex}};
}

inline void from_json(const nlohmann::json& json, CaptureConfig& config) {
    constexpr auto kMaxPreferredDisplayIndex =
        static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max());

    const nlohmann::json& value = json.at("preferredDisplayIndex");
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw nlohmann::json::type_error::create(
            detail::kJsonTypeErrorId, "expected integer for key 'preferredDisplayIndex'", &value);
    }

    if (value.is_number_unsigned()) {
        const auto unsignedValue = value.get<unsigned long long>();
        if (unsignedValue > kMaxPreferredDisplayIndex) {
            throw nlohmann::json::other_error::create(
                detail::kJsonOtherErrorId, "out of range for key 'preferredDisplayIndex'", &value);
        }
        config.preferredDisplayIndex = static_cast<std::uint32_t>(unsignedValue);
        return;
    }

    const auto signedValue = value.get<long long>();
    if (signedValue < 0 ||
        static_cast<unsigned long long>(signedValue) > kMaxPreferredDisplayIndex) {
        throw nlohmann::json::other_error::create(
            detail::kJsonOtherErrorId, "out of range for key 'preferredDisplayIndex'", &value);
    }
    config.preferredDisplayIndex = static_cast<std::uint32_t>(signedValue);
}

inline void to_json(nlohmann::json& json, const VisionFlowConfig& config) {
    json = {
        {"app", config.app},
        {"makcu", config.makcu},
        {"capture", config.capture},
    };
}

inline void from_json(const nlohmann::json& json, VisionFlowConfig& config) {
    config.app = json.at("app").get<AppConfig>();
    config.makcu = json.at("makcu").get<MakcuConfig>();
    if (json.contains("capture")) {
        config.capture = json.at("capture").get<CaptureConfig>();
    }
}
// NOLINTEND(readability-identifier-naming)

} // namespace vf
