#pragma once

#include <expected>
#include <filesystem>
#include <system_error>

#include "VisionFlow/core/config.hpp"

namespace vf {

[[nodiscard]] std::expected<VisionFlowConfig, std::error_code>
loadConfig(const std::filesystem::path& path);

} // namespace vf
