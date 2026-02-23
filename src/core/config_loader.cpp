#include "VisionFlow/core/config_loader.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "VisionFlow/core/config_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "core/config_json.hpp"

namespace vf {

namespace {

[[nodiscard]] std::expected<VisionFlowConfig, std::error_code>
createDefaultConfigFile(const std::filesystem::path& path) {
    VisionFlowConfig config{};

    const std::filesystem::path parentPath = path.parent_path();
    if (!parentPath.empty()) {
        std::error_code directoryError;
        const bool directoryCreated =
            std::filesystem::create_directories(parentPath, directoryError);
        static_cast<void>(directoryCreated);
        if (directoryError) {
            VF_ERROR("Config directory create failed '{}': {}", parentPath.string(),
                     directoryError.message());
            return std::unexpected(makeErrorCode(ConfigError::FileNotFound));
        }
    }

    nlohmann::json root = config;

    std::ofstream stream(path, std::ios::trunc);
    if (!stream.is_open()) {
        VF_ERROR("Config default file create failed: {}", path.string());
        return std::unexpected(makeErrorCode(ConfigError::FileNotFound));
    }

    stream << root.dump(2) << '\n';
    if (!stream.good()) {
        VF_ERROR("Config default file write failed: {}", path.string());
        return std::unexpected(makeErrorCode(ConfigError::FileNotFound));
    }

    VF_WARN("Config file not found. Created default config at '{}'", path.string());
    return config;
}

} // namespace

std::expected<VisionFlowConfig, std::error_code> loadConfig(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        std::error_code existsError;
        const bool fileExists = std::filesystem::exists(path, existsError);
        if (existsError) {
            VF_ERROR("Config path check failed '{}': {}", path.string(), existsError.message());
            return std::unexpected(makeErrorCode(ConfigError::ParseFailed));
        }
        if (!fileExists) {
            return createDefaultConfigFile(path);
        }

        VF_ERROR("Config open failed for existing path: {}", path.string());
        return std::unexpected(makeErrorCode(ConfigError::ParseFailed));
    }

    nlohmann::json root;
    try {
        stream >> root;
        return root.get<VisionFlowConfig>();
    } catch (const nlohmann::json::out_of_range& ex) {
        VF_ERROR("Config missing key in '{}': {}", path.string(), ex.what());
        return std::unexpected(makeErrorCode(ConfigError::MissingKey));
    } catch (const nlohmann::json::type_error& ex) {
        VF_ERROR("Config type error in '{}': {}", path.string(), ex.what());
        return std::unexpected(makeErrorCode(ConfigError::InvalidType));
    } catch (const nlohmann::json::other_error& ex) {
        VF_ERROR("Config range error in '{}': {}", path.string(), ex.what());
        return std::unexpected(makeErrorCode(ConfigError::OutOfRange));
    } catch (const nlohmann::json::exception& ex) {
        VF_ERROR("Config parse failed '{}': {}", path.string(), ex.what());
        return std::unexpected(makeErrorCode(ConfigError::ParseFailed));
    }
}

} // namespace vf
