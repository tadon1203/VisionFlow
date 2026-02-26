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
  "inference": { "modelPath": "detector.onnx" }
})");

    const auto result = loadConfig(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->app.reconnectRetryMs, std::chrono::milliseconds(500));
    EXPECT_EQ(result->makcu.remainderTtlMs, std::chrono::milliseconds(200));
    EXPECT_EQ(result->capture.preferredDisplayIndex, 1U);
    EXPECT_EQ(result->inference.modelPath, "detector.onnx");

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

    static_cast<void>(std::filesystem::remove(path));
}

TEST(ConfigLoaderTest, ReturnsInvalidTypeForInferenceModelPath) {
    const auto path = makeTempPath("visionflow_config_inference_invalid_type.json");
    writeText(path,
              R"({
  "app": { "reconnectRetryMs": 500 },
  "makcu": { "remainderTtlMs": 200 },
  "inference": { "modelPath": 1234 }
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
  "inference": { "modelPath": "" }
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

} // namespace
} // namespace vf
