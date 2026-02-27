#include "VisionFlow/core/app.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VisionFlow/capture/i_capture_runtime.hpp"
#include "VisionFlow/core/config.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/i_inference_result_store.hpp"
#include "VisionFlow/input/i_mouse_controller.hpp"

namespace vf {
namespace {

class MockMouseController : public IMouseController {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), connect, (), (override));
    MOCK_METHOD(State, getState, (), (const, override));
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

class MockInferenceProcessor : public IInferenceProcessor {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), start, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), stop, (), (override));
};

class MockInferenceResultStore : public IInferenceResultStore {
  public:
    MOCK_METHOD(void, publish, (InferenceResult result), (override));
    MOCK_METHOD((std::optional<InferenceResult>), take, (), (override));
};

TEST(AppTest, RunReturnsFalseWhenControllerIsNull) {
    App app(nullptr, AppConfig{});
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunReturnsFalseWhenCaptureStartFails) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto store = std::make_unique<testing::StrictMock<MockInferenceResultStore>>();

    EXPECT_CALL(*inference, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capture, start(testing::_))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*inference, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunReturnsFalseWhenInferenceStartFails) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto store = std::make_unique<testing::StrictMock<MockInferenceResultStore>>();

    EXPECT_CALL(*inference, start())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunReturnsFalseWhenConnectFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<testing::StrictMock<MockInferenceResultStore>>();
    auto* storePtr = store.get();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*mockPtr, getState()).WillOnce(testing::Return(IMouseController::State::Idle));
    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, shouldRetryConnect(testing::_)).WillOnce(testing::Return(false));
    EXPECT_CALL(*storePtr, take()).Times(0);
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mockPtr, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mock), AppConfig{}, CaptureConfig{}, std::move(capture), std::move(inference),
            std::move(store));
    EXPECT_FALSE(app.run());
}

TEST(AppTest, RunRetriesConnectForRecoverableErrorThenFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<testing::StrictMock<MockInferenceResultStore>>();
    auto* storePtr = store.get();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*mockPtr, getState())
        .WillOnce(testing::Return(IMouseController::State::Idle))
        .WillOnce(testing::Return(IMouseController::State::Idle));
    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::timed_out))))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, shouldRetryConnect(testing::_))
        .WillOnce(testing::Return(true))
        .WillOnce(testing::Return(false));
    EXPECT_CALL(*storePtr, take()).Times(0);
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mockPtr, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mock), AppConfig{}, CaptureConfig{}, std::move(capture), std::move(inference),
            std::move(store));
    EXPECT_FALSE(app.run());
}

TEST(AppTest, ShutdownOrderIsCaptureThenInferenceThenMouse) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureRuntime>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<testing::StrictMock<MockInferenceResultStore>>();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mousePtr, getState()).WillOnce(testing::Return(IMouseController::State::Idle));
    EXPECT_CALL(*mousePtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mousePtr, shouldRetryConnect(testing::_)).WillOnce(testing::Return(false));

    {
        testing::InSequence sequence;
        EXPECT_CALL(*capturePtr, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*inferencePtr, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*mousePtr, disconnect())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    }

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    EXPECT_FALSE(app.run());
}

} // namespace
} // namespace vf
