#include "VisionFlow/core/config_loader.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "VisionFlow/core/config_error.hpp"

namespace vf {
namespace {

std::filesystem::path makeTempPath(const std::string& fileName) {
    static std::atomic<std::uint64_t> sequence{0};
    const std::uint64_t id = sequence.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path() / (std::to_string(id) + "_" + fileName);
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::trunc);
    ASSERT_TRUE(stream.is_open());
    stream << text;
}

TEST(ConfigLoaderTest, LoadsValidConfig) {
    const auto path = makeTempPath("visionflow_config_valid.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "capture": { "preferredDisplayIndex": 1 },
  "inference": {
    "modelPath": "detector.onnx",
    "confidenceThreshold": 0.4
  },
  "aim": {
    "aimStrength": 0.6,
    "aimMaxStep": 110,
    "triggerThreshold": 0.7,
    "activationButtons": [["Mouse:Right", "Key:Shift", "Pad:LT"]]
  },
  "profiler": { "enabled": true, "reportIntervalMs": 250 }
})");

    const auto result = loadConfig(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->app.reconnectRetryMs, std::chrono::milliseconds(500));
    EXPECT_EQ(result->makcu.remainderTtlMs, std::chrono::milliseconds(200));
    EXPECT_EQ(result->capture.preferredDisplayIndex, 1U);
    EXPECT_EQ(result->inference.modelPath, "detector.onnx");
    EXPECT_FLOAT_EQ(result->inference.confidenceThreshold, 0.4F);
    EXPECT_FLOAT_EQ(result->aim.aimStrength, 0.6F);
    EXPECT_EQ(result->aim.aimMaxStep, 110);
    EXPECT_FLOAT_EQ(result->aim.triggerThreshold, 0.7F);
    ASSERT_EQ(result->aim.activationButtons.size(), 1U);
    ASSERT_EQ(result->aim.activationButtons.front().size(), 3U);
    EXPECT_TRUE(result->profiler.enabled);
    EXPECT_EQ(result->profiler.reportIntervalMs, std::chrono::milliseconds(250));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, CreatesDefaultConfigForMissingFile) {
    const auto path = makeTempPath("visionflow_config_missing.json");
    static_cast<void>(std::filesystem::remove(path));

    const auto result = loadConfig(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->app.reconnectRetryMs, std::chrono::milliseconds(500));
    EXPECT_EQ(result->makcu.remainderTtlMs, std::chrono::milliseconds(200));
    EXPECT_EQ(result->capture.preferredDisplayIndex, 0U);
    EXPECT_EQ(result->inference.modelPath, "model.onnx");
    EXPECT_FLOAT_EQ(result->inference.confidenceThreshold, 0.25F);
    EXPECT_FLOAT_EQ(result->aim.aimStrength, 0.4F);
    EXPECT_EQ(result->aim.aimMaxStep, 127);
    EXPECT_FLOAT_EQ(result->aim.triggerThreshold, 0.5F);
    EXPECT_TRUE(result->aim.activationButtons.empty());
    EXPECT_FALSE(result->profiler.enabled);
    EXPECT_EQ(result->profiler.reportIntervalMs, std::chrono::milliseconds(1000));
    EXPECT_TRUE(std::filesystem::exists(path));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsMissingKeyForAbsentField) {
    const auto path = makeTempPath("visionflow_config_missing_key.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": {}
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::MissingKey));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForNonIntegerMs) {
    const auto path = makeTempPath("visionflow_config_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": "500" },
  "makcu": { "remainderTtlMs": 200 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForNonPositiveMs) {
    const auto path = makeTempPath("visionflow_config_out_of_range.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 0 },
  "makcu": { "remainderTtlMs": 200 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForTooLargeUnsignedMs) {
    const auto path = makeTempPath("visionflow_config_out_of_range_large.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 18446744073709551615 },
  "makcu": { "remainderTtlMs": 200 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsParseFailedForMalformedJson) {
    const auto path = makeTempPath("visionflow_config_malformed.json");
    writeText(path, R"({ "app": { "reconnectRetryMs": 500 },)");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::ParseFailed));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsParseFailedWhenPathIsDirectory) {
    const auto path = makeTempPath("visionflow_config_directory");
    std::error_code createError;
    static_cast<void>(std::filesystem::create_directories(path, createError));
    ASSERT_FALSE(createError);

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::ParseFailed));

    std::error_code removeError;
    static_cast<void>(std::filesystem::remove_all(path, removeError));
    ASSERT_FALSE(removeError);
}

TEST(ConfigLoaderTest, UsesDefaultCaptureConfigWhenCaptureSectionMissing) {
    const auto path = makeTempPath("visionflow_config_without_capture.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 }
})");

    const auto result = loadConfig(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->capture.preferredDisplayIndex, 0U);
    EXPECT_EQ(result->inference.modelPath, "model.onnx");
    EXPECT_FLOAT_EQ(result->inference.confidenceThreshold, 0.25F);
    EXPECT_FLOAT_EQ(result->aim.aimStrength, 0.4F);
    EXPECT_EQ(result->aim.aimMaxStep, 127);
    EXPECT_FLOAT_EQ(result->aim.triggerThreshold, 0.5F);
    EXPECT_TRUE(result->aim.activationButtons.empty());

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForInferenceModelPath) {
    const auto path = makeTempPath("visionflow_config_inference_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "inference": { "modelPath": 1234, "confidenceThreshold": 0.25 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForEmptyInferenceModelPath) {
    const auto path = makeTempPath("visionflow_config_inference_empty_path.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "inference": { "modelPath": "", "confidenceThreshold": 0.25 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, UsesDefaultInferenceConfidenceThresholdWhenMissing) {
    const auto path = makeTempPath("visionflow_config_inference_threshold_missing.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "inference": { "modelPath": "model.onnx" }
})");

    const auto result = loadConfig(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->inference.confidenceThreshold, 0.25F);
    EXPECT_FLOAT_EQ(result->aim.aimStrength, 0.4F);
    EXPECT_EQ(result->aim.aimMaxStep, 127);
    EXPECT_FLOAT_EQ(result->aim.triggerThreshold, 0.5F);
    EXPECT_TRUE(result->aim.activationButtons.empty());

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForInferenceConfidenceThreshold) {
    const auto path = makeTempPath("visionflow_config_inference_threshold_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "inference": { "modelPath": "model.onnx", "confidenceThreshold": "0.25" }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForInferenceConfidenceThreshold) {
    const auto path = makeTempPath("visionflow_config_inference_threshold_out_of_range.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "inference": { "modelPath": "model.onnx", "confidenceThreshold": 1.1 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForAimStrength) {
    const auto path = makeTempPath("visionflow_config_aim_strength_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "aimStrength": "0.4" }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForAimStrength) {
    const auto path = makeTempPath("visionflow_config_aim_strength_out_of_range.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "aimStrength": 0.0 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForAimMaxStep) {
    const auto path = makeTempPath("visionflow_config_aim_max_step_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "aimMaxStep": 2.5 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForAimMaxStep) {
    const auto path = makeTempPath("visionflow_config_aim_max_step_out_of_range.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "aimMaxStep": 128 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForAimTriggerThreshold) {
    const auto path = makeTempPath("visionflow_config_aim_trigger_threshold_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "triggerThreshold": "0.5" }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForAimTriggerThreshold) {
    const auto path = makeTempPath("visionflow_config_aim_trigger_threshold_out_of_range.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "triggerThreshold": 1.1 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForAimActivationButtons) {
    const auto path = makeTempPath("visionflow_config_aim_activation_buttons_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "activationButtons": "Mouse:Right" }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForAimActivationButtonsMultipleRows) {
    const auto path = makeTempPath("visionflow_config_aim_activation_buttons_multiple_rows.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "activationButtons": [["Mouse:Right"], ["Key:Shift"]] }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForUnknownAimActivationButtonToken) {
    const auto path = makeTempPath("visionflow_config_aim_activation_buttons_unknown_token.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "aim": { "activationButtons": [["Mouse:Unknown"]] }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForNegativePreferredDisplayIndex) {
    const auto path = makeTempPath("visionflow_config_capture_negative_index.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "capture": { "preferredDisplayIndex": -1 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForTooLargePreferredDisplayIndex) {
    const auto path = makeTempPath("visionflow_config_capture_large_index.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "capture": { "preferredDisplayIndex": 4294967296 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, UsesDefaultProfilerConfigWhenProfilerSectionMissing) {
    const auto path = makeTempPath("visionflow_config_without_profiler.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 }
})");

    const auto result = loadConfig(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->profiler.enabled);
    EXPECT_EQ(result->profiler.reportIntervalMs, std::chrono::milliseconds(1000));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForProfilerEnabled) {
    const auto path = makeTempPath("visionflow_config_profiler_enabled_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "profiler": { "enabled": 1, "reportIntervalMs": 1000 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::InvalidType));

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsOutOfRangeForProfilerReportIntervalMs) {
    const auto path = makeTempPath("visionflow_config_profiler_interval_out_of_range.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "profiler": { "enabled": true, "reportIntervalMs": 0 }
})");

    const auto result = loadConfig(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(ConfigError::OutOfRange));

    static_cast<void>(std::filesystem::remove(path));
}

} // namespace
} // namespace vf
