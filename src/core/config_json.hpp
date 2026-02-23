#pragma once

#include <chrono>
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

inline void to_json(nlohmann::json& json, const VisionFlowConfig& config) {
    json = {
        {"app", config.app},
        {"makcu", config.makcu},
    };
}

inline void from_json(const nlohmann::json& json, VisionFlowConfig& config) {
    config.app = json.at("app").get<AppConfig>();
    config.makcu = json.at("makcu").get<MakcuConfig>();
}

} // namespace vf
