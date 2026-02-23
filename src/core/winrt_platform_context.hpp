#pragma once

#include <expected>
#include <system_error>

namespace vf {

class WinRtPlatformContext final {
  public:
    WinRtPlatformContext() = default;
    WinRtPlatformContext(const WinRtPlatformContext&) = delete;
    WinRtPlatformContext(WinRtPlatformContext&&) = delete;
    WinRtPlatformContext& operator=(const WinRtPlatformContext&) = delete;
    WinRtPlatformContext& operator=(WinRtPlatformContext&&) = delete;
    ~WinRtPlatformContext();

    [[nodiscard]] std::expected<void, std::error_code> initialize();

  private:
    bool initialized = false;
};

} // namespace vf
