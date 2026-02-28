#pragma once

#include <expected>
#include <system_error>

namespace vf {

class WinrtPlatformContext final {
  public:
    WinrtPlatformContext() = default;
    WinrtPlatformContext(const WinrtPlatformContext&) = delete;
    WinrtPlatformContext(WinrtPlatformContext&&) = delete;
    WinrtPlatformContext& operator=(const WinrtPlatformContext&) = delete;
    WinrtPlatformContext& operator=(WinrtPlatformContext&&) = delete;
    ~WinrtPlatformContext();

    [[nodiscard]] std::expected<void, std::error_code> initialize();

  private:
    bool initialized = false;
};

} // namespace vf
