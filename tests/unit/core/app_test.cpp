#include "VisionFlow/core/app.hpp"

#include <expected>
#include <memory>
#include <system_error>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VisionFlow/capture/i_capture_runtime.hpp"
#include "VisionFlow/core/config.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {
namespace {

class MockMouseController : public IMouseController {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), connect, (), (override));
    MOCK_METHOD(bool, shouldRetryConnect, (const std::error_code& error), (const, override));
    MOCK_METHOD((std::expected<void, std::error_code>), disconnect, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), move, (float dx, float dy), (override));
};

class MockCaptureRuntime : public ICaptureRuntime {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), start, (const CaptureConfig& config),
                (override));
    MOCK_METHOD((std::expected<void, std::error_code>), stop, (), (override));
};

TEST(AppTest, RunReturnsFalseWhenControllerIsNull) {
    App app(nullptr, AppConfig{});
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunReturnsFalseWhenCaptureStartFails) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();

    EXPECT_CALL(*capture, start(testing::_))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, std::move(capture));
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunReturnsFalseWhenConnectFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto* capturePtr = capture.get();

    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, shouldRetryConnect(testing::_)).WillOnce(testing::Return(false));
    EXPECT_CALL(*mockPtr, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mock), AppConfig{}, CaptureConfig{}, std::move(capture));
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunRetriesConnectForRecoverableErrorThenFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto* capturePtr = capture.get();

    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::timed_out))))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, shouldRetryConnect(testing::_))
        .WillOnce(testing::Return(true))
        .WillOnce(testing::Return(false));
    EXPECT_CALL(*mockPtr, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mock), AppConfig{}, CaptureConfig{}, std::move(capture));
    EXPECT_FALSE(app.run());
}

} // namespace
} // namespace vf
