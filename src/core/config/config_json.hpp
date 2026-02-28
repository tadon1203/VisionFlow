#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "VisionFlow/core/config.hpp"

namespace vf {

namespace detail {

constexpr int kJsonTypeErrorId = 302;
constexpr int kJsonOutOfRangeId = 403;
constexpr int kJsonOtherErrorId = 501;

[[nodiscard]] inline std::string toUpperAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }
    return text;
}

[[nodiscard]] inline bool isKnownAimButtonToken(const std::string& token) {
    const std::string upper = toUpperAscii(token);
    const auto separatorPos = upper.find(':');
    if (separatorPos == std::string::npos || separatorPos == 0U ||
        separatorPos == (upper.size() - 1U)) {
        return false;
    }

    const std::string prefix = upper.substr(0, separatorPos);
    const std::string name = upper.substr(separatorPos + 1U);
    if (prefix == "KEY") {
        if (name.size() == 1U) {
            const char c = name.front();
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                return true;
            }
        }

        static const std::vector<std::string> kKeyNames = {
            "SHIFT", "CTRL", "ALT", "SPACE", "TAB", "ESC", "ENTER", "UP", "DOWN", "LEFT", "RIGHT",
        };
        return std::ranges::find(kKeyNames, name) != kKeyNames.end();
    }

    if (prefix == "MOUSE") {
        static const std::vector<std::string> kMouseNames = {
            "LEFT", "RIGHT", "MIDDLE", "X1", "X2",
        };
        return std::ranges::find(kMouseNames, name) != kMouseNames.end();
    }

    if (prefix == "PAD") {
        static const std::vector<std::string> kPadNames = {
            "A",      "B",      "X",      "Y",        "LB",       "RB",        "BACK", "START",
            "LTHUMB", "RTHUMB", "DPADUP", "DPADDOWN", "DPADLEFT", "DPADRIGHT", "LT",   "RT",
        };
        return std::ranges::find(kPadNames, name) != kPadNames.end();
    }

    return false;
}

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
    if (signedValue < 0 || std::cmp_greater(signedValue, kMaxPreferredDisplayIndex)) {
        throw nlohmann::json::other_error::create(
            detail::kJsonOtherErrorId, "out of range for key 'preferredDisplayIndex'", &value);
    }
    config.preferredDisplayIndex = static_cast<std::uint32_t>(signedValue);
}

inline void to_json(nlohmann::json& json, const InferenceConfig& config) {
    json = {
        {"modelPath", config.modelPath},
        {"confidenceThreshold", config.confidenceThreshold},
    };
}

inline void from_json(const nlohmann::json& json, InferenceConfig& config) {
    const nlohmann::json& modelPathValue = json.at("modelPath");
    if (!modelPathValue.is_string()) {
        throw nlohmann::json::type_error::create(
            detail::kJsonTypeErrorId, "expected string for key 'modelPath'", &modelPathValue);
    }

    config.modelPath = modelPathValue.get<std::string>();
    if (config.modelPath.empty()) {
        throw nlohmann::json::other_error::create(
            detail::kJsonOtherErrorId, "out of range for key 'modelPath'", &modelPathValue);
    }

    if (json.contains("confidenceThreshold")) {
        const nlohmann::json& thresholdValue = json.at("confidenceThreshold");
        if (!thresholdValue.is_number_float() && !thresholdValue.is_number_integer() &&
            !thresholdValue.is_number_unsigned()) {
            throw nlohmann::json::type_error::create(
                detail::kJsonTypeErrorId, "expected number for key 'confidenceThreshold'",
                &thresholdValue);
        }

        config.confidenceThreshold = thresholdValue.get<float>();
        if (!std::isfinite(config.confidenceThreshold) || config.confidenceThreshold < 0.0F ||
            config.confidenceThreshold > 1.0F) {
            throw nlohmann::json::other_error::create(detail::kJsonOtherErrorId,
                                                      "out of range for key 'confidenceThreshold'",
                                                      &thresholdValue);
        }
    }
}

inline void to_json(nlohmann::json& json, const AimConfig& config) {
    json = {
        {"aimStrength", config.aimStrength},
        {"aimMaxStep", config.aimMaxStep},
        {"triggerThreshold", config.triggerThreshold},
        {"activationButtons", config.activationButtons},
    };
}

inline void from_json(const nlohmann::json& json, AimConfig& config) {
    if (json.contains("aimStrength")) {
        const nlohmann::json& aimStrengthValue = json.at("aimStrength");
        if (!aimStrengthValue.is_number_float() && !aimStrengthValue.is_number_integer() &&
            !aimStrengthValue.is_number_unsigned()) {
            throw nlohmann::json::type_error::create(detail::kJsonTypeErrorId,
                                                     "expected number for key 'aimStrength'",
                                                     &aimStrengthValue);
        }

        config.aimStrength = aimStrengthValue.get<float>();
        if (!std::isfinite(config.aimStrength) || config.aimStrength <= 0.0F) {
            throw nlohmann::json::other_error::create(
                detail::kJsonOtherErrorId, "out of range for key 'aimStrength'", &aimStrengthValue);
        }
    }

    if (json.contains("aimMaxStep")) {
        constexpr unsigned long long kMaxAimMaxStep = 127ULL;
        const nlohmann::json& aimMaxStepValue = json.at("aimMaxStep");
        if (!aimMaxStepValue.is_number_integer()) {
            throw nlohmann::json::type_error::create(detail::kJsonTypeErrorId,
                                                     "expected integer for key 'aimMaxStep'",
                                                     &aimMaxStepValue);
        }

        if (aimMaxStepValue.is_number_unsigned()) {
            const auto value = aimMaxStepValue.get<unsigned long long>();
            if (value < 1ULL || value > kMaxAimMaxStep) {
                throw nlohmann::json::other_error::create(detail::kJsonOtherErrorId,
                                                          "out of range for key 'aimMaxStep'",
                                                          &aimMaxStepValue);
            }
            config.aimMaxStep = static_cast<std::int32_t>(value);
        } else {
            const auto value = aimMaxStepValue.get<long long>();
            if (value < 1LL || value > static_cast<long long>(kMaxAimMaxStep)) {
                throw nlohmann::json::other_error::create(detail::kJsonOtherErrorId,
                                                          "out of range for key 'aimMaxStep'",
                                                          &aimMaxStepValue);
            }
            config.aimMaxStep = static_cast<std::int32_t>(value);
        }
    }

    if (json.contains("triggerThreshold")) {
        const nlohmann::json& thresholdValue = json.at("triggerThreshold");
        if (!thresholdValue.is_number_float() && !thresholdValue.is_number_integer() &&
            !thresholdValue.is_number_unsigned()) {
            throw nlohmann::json::type_error::create(detail::kJsonTypeErrorId,
                                                     "expected number for key 'triggerThreshold'",
                                                     &thresholdValue);
        }
        config.triggerThreshold = thresholdValue.get<float>();
        if (!std::isfinite(config.triggerThreshold) || config.triggerThreshold < 0.0F ||
            config.triggerThreshold > 1.0F) {
            throw nlohmann::json::other_error::create(detail::kJsonOtherErrorId,
                                                      "out of range for key 'triggerThreshold'",
                                                      &thresholdValue);
        }
    }

    if (json.contains("activationButtons")) {
        const nlohmann::json& activationButtonsValue = json.at("activationButtons");
        if (!activationButtonsValue.is_array()) {
            throw nlohmann::json::type_error::create(detail::kJsonTypeErrorId,
                                                     "expected array for key 'activationButtons'",
                                                     &activationButtonsValue);
        }
        if (activationButtonsValue.size() > 1U) {
            throw nlohmann::json::other_error::create(detail::kJsonOtherErrorId,
                                                      "out of range for key 'activationButtons'",
                                                      &activationButtonsValue);
        }

        config.activationButtons.clear();
        config.activationButtons.reserve(activationButtonsValue.size());
        for (const nlohmann::json& comboValue : activationButtonsValue) {
            if (!comboValue.is_array()) {
                throw nlohmann::json::type_error::create(
                    detail::kJsonTypeErrorId, "expected array row for key 'activationButtons'",
                    &comboValue);
            }

            std::vector<std::string> combo;
            combo.reserve(comboValue.size());
            for (const nlohmann::json& tokenValue : comboValue) {
                if (!tokenValue.is_string()) {
                    throw nlohmann::json::type_error::create(
                        detail::kJsonTypeErrorId,
                        "expected string token for key 'activationButtons'", &tokenValue);
                }

                const std::string token = tokenValue.get<std::string>();
                if (token.empty() || !detail::isKnownAimButtonToken(token)) {
                    throw nlohmann::json::other_error::create(
                        detail::kJsonOtherErrorId, "out of range for key 'activationButtons'",
                        &tokenValue);
                }
                combo.push_back(token);
            }
            config.activationButtons.push_back(std::move(combo));
        }
    }
}

inline void to_json(nlohmann::json& json, const ProfilerConfig& config) {
    json = {
        {"enabled", config.enabled},
        {"reportIntervalMs", config.reportIntervalMs.count()},
    };
}

inline void from_json(const nlohmann::json& json, ProfilerConfig& config) {
    const nlohmann::json& enabledValue = json.at("enabled");
    if (!enabledValue.is_boolean()) {
        throw nlohmann::json::type_error::create(
            detail::kJsonTypeErrorId, "expected boolean for key 'enabled'", &enabledValue);
    }
    config.enabled = enabledValue.get<bool>();
    config.reportIntervalMs = detail::readPositiveMilliseconds(json, "reportIntervalMs");
}

inline void to_json(nlohmann::json& json, const VisionFlowConfig& config) {
    json = {
        {"app", config.app},         {"makcu", config.makcu},
        {"capture", config.capture}, {"inference", config.inference},
        {"aim", config.aim},         {"profiler", config.profiler},
    };
}

inline void from_json(const nlohmann::json& json, VisionFlowConfig& config) {
    config.app = json.at("app").get<AppConfig>();
    config.makcu = json.at("makcu").get<MakcuConfig>();
    if (json.contains("capture")) {
        config.capture = json.at("capture").get<CaptureConfig>();
    }
    if (json.contains("inference")) {
        config.inference = json.at("inference").get<InferenceConfig>();
    }
    if (json.contains("aim")) {
        config.aim = json.at("aim").get<AimConfig>();
    }
    if (json.contains("profiler")) {
        config.profiler = json.at("profiler").get<ProfilerConfig>();
    }
}
// NOLINTEND(readability-identifier-naming)

} // namespace vf
