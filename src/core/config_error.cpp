#include "VisionFlow/core/config_error.hpp"

#include <string_view>
#include <system_error>

namespace vf {

const char* ErrorDomainTraits<ConfigError>::domainName() noexcept { return "config"; }

std::string_view ErrorDomainTraits<ConfigError>::unknownMessage() noexcept {
    return "unknown config error";
}

std::string_view ErrorDomainTraits<ConfigError>::message(ConfigError error) noexcept {
    switch (error) {
    case ConfigError::FileNotFound:
        return "config file not found";
    case ConfigError::ParseFailed:
        return "config json parse failed";
    case ConfigError::MissingKey:
        return "config key missing";
    case ConfigError::InvalidType:
        return "config value has invalid type";
    case ConfigError::OutOfRange:
        return "config value out of range";
    default:
        return {};
    }
}

const std::error_category& configErrorCategory() noexcept { return errorCategory<ConfigError>(); }

std::error_code makeErrorCode(ConfigError error) noexcept {
    return makeErrorCode<ConfigError>(error);
}

} // namespace vf
