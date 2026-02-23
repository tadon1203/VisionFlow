#include "VisionFlow/input/mouse_error.hpp"

#include <system_error>

#include <gtest/gtest.h>

namespace vf {
namespace {

TEST(MouseErrorTest, ShouldRetryConnectErrorReturnsFalseForPlatformNotSupported) {
    const bool retry = shouldRetryConnectError(makeErrorCode(MouseError::PlatformNotSupported));
    EXPECT_FALSE(retry);
}

TEST(MouseErrorTest, ShouldRetryConnectErrorReturnsTrueForRecoverableMouseError) {
    const bool retry = shouldRetryConnectError(makeErrorCode(MouseError::PortNotFound));
    EXPECT_TRUE(retry);
}

TEST(MouseErrorTest, ShouldRetryConnectErrorReturnsFalseForOtherErrorCategory) {
    const bool retry = shouldRetryConnectError(std::make_error_code(std::errc::io_error));
    EXPECT_FALSE(retry);
}

} // namespace
} // namespace vf
