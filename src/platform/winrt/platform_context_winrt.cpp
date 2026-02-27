#include "platform/winrt/platform_context_winrt.hpp"

#include <expected>
#include <system_error>

#include "VisionFlow/core/app_error.hpp"

#ifdef _WIN32
#include <winrt/base.h>
#endif

namespace vf {

WinRtPlatformContext::~WinRtPlatformContext() {
#ifdef _WIN32
    if (initialized) {
        winrt::uninit_apartment();
    }
#endif
}

std::expected<void, std::error_code> WinRtPlatformContext::initialize() {
#ifdef _WIN32
    if (initialized) {
        return {};
    }

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        initialized = true;
        return {};
    } catch (const winrt::hresult_error&) {
        return std::unexpected(makeErrorCode(AppError::PlatformInitFailed));
    }
#else
    return std::unexpected(makeErrorCode(AppError::PlatformInitFailed));
#endif
}

} // namespace vf
