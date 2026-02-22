#include "VisionFlow/core/app.hpp"

#include <expected>
#include <memory>
#include <system_error>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {
namespace {

class MockMouseController : public IMouseController {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), connect, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), disconnect, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), move, (int dx, int dy), (override));
};

TEST(AppTest, RunReturnsFalseWhenControllerIsNull) {
    App app(nullptr);
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunReturnsFalseWhenConnectFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();

    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, disconnect()).Times(0);

    App app(std::move(mock));
    EXPECT_FALSE(app.run());
}

} // namespace
} // namespace vf
