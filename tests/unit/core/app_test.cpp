#include "VisionFlow/core/app.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VisionFlow/capture/i_capture_source.hpp"
#include "VisionFlow/core/config.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "VisionFlow/inference/inference_result_store.hpp"
#include "VisionFlow/input/i_aim_activation_input.hpp"
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

class MockCaptureSource : public ICaptureSource {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), start, (const CaptureConfig& config),
                (override));
    MOCK_METHOD((std::expected<void, std::error_code>), stop, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), poll, (), (override));
};

class MockInferenceProcessor : public IInferenceProcessor {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), start, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), stop, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), poll, (), (override));
};

class MockAimActivationInput : public IAimActivationInput {
  public:
    MOCK_METHOD(bool, isAimActivationPressed, (), (const, override));
};

TEST(AppTest, RunReturnsInvalidArgumentWhenControllerIsNull) {
    App app(nullptr, AppConfig{}, CaptureConfig{}, AimConfig{}, nullptr, nullptr, nullptr);
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(AppTest, RunPropagatesCaptureStartError) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto store = std::make_unique<InferenceResultStore>();

    EXPECT_CALL(*inference, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capture, start(testing::_))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    {
        testing::InSequence sequence;
        EXPECT_CALL(*capture, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*inference, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    }

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunPropagatesInferenceStartError) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto store = std::make_unique<InferenceResultStore>();

    EXPECT_CALL(*inference, start())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunPropagatesCapturePollError) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(
            std::unexpected(std::make_error_code(std::errc::state_not_recoverable))));
    EXPECT_CALL(*inferencePtr, poll()).Times(0);
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mouse, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::state_not_recoverable));
}

TEST(AppTest, RunReturnsFalseWhenConnectFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, shouldRetryConnect(testing::_)).WillOnce(testing::Return(false));
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mockPtr, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mock), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunRetriesConnectForRecoverableErrorThenFails) {
    auto mock = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mockPtr = mock.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    EXPECT_CALL(*mockPtr, connect())
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::timed_out))))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mockPtr, shouldRetryConnect(testing::_))
        .WillOnce(testing::Return(true))
        .WillOnce(testing::Return(false));
    EXPECT_CALL(*capturePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, stop())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mockPtr, disconnect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    App app(std::move(mock), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
}

TEST(AppTest, RunAppliesCenterPriorityMoveAndPropagatesMoveError) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto aimInput = std::make_unique<testing::StrictMock<MockAimActivationInput>>();
    auto* aimInputPtr = aimInput.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 500.0F,
        .centerY = 320.0F,
        .width = 20.0F,
        .height = 20.0F,
        .score = 0.95F,
        .classId = 0,
    });
    result.detections.emplace_back(InferenceDetection{
        .centerX = 330.0F,
        .centerY = 320.0F,
        .width = 20.0F,
        .height = 20.0F,
        .score = 0.40F,
        .classId = 0,
    });
    store->publish(std::move(result));

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mousePtr, connect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*aimInputPtr, isAimActivationPressed()).WillOnce(testing::Return(true));
    EXPECT_CALL(*mousePtr, move(4.0F, 0.0F))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));

    {
        testing::InSequence sequence;
        EXPECT_CALL(*capturePtr, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*inferencePtr, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*mousePtr, disconnect())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    }

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store), std::move(aimInput));
    const auto runResult = app.run();
    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunClampsMoveByAimMaxStep) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto aimInput = std::make_unique<testing::StrictMock<MockAimActivationInput>>();
    auto* aimInputPtr = aimInput.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 640.0F,
        .centerY = 0.0F,
        .width = 20.0F,
        .height = 20.0F,
        .score = 0.90F,
        .classId = 0,
    });
    store->publish(std::move(result));

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mousePtr, connect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*aimInputPtr, isAimActivationPressed()).WillOnce(testing::Return(true));
    EXPECT_CALL(*mousePtr, move(127.0F, -127.0F))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));

    {
        testing::InSequence sequence;
        EXPECT_CALL(*capturePtr, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*inferencePtr, stop())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
        EXPECT_CALL(*mousePtr, disconnect())
            .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    }

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store), std::move(aimInput));
    const auto runResult = app.run();
    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunDoesNotMoveWhenNoDetections) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    store->publish(InferenceResult{});

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mousePtr, connect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*mousePtr, move(testing::_, testing::_)).Times(0);
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

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto runResult = app.run();
    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunSkipsMoveWhenRoundedDeltaIsZero) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto aimInput = std::make_unique<testing::StrictMock<MockAimActivationInput>>();
    auto* aimInputPtr = aimInput.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 320.2F,
        .centerY = 319.9F,
        .width = 20.0F,
        .height = 20.0F,
        .score = 0.90F,
        .classId = 0,
    });
    store->publish(std::move(result));

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mousePtr, connect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*aimInputPtr, isAimActivationPressed()).WillOnce(testing::Return(true));
    EXPECT_CALL(*mousePtr, move(testing::_, testing::_)).Times(0);
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

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store), std::move(aimInput));
    const auto runResult = app.run();
    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, RunDoesNotMoveWhenAimActivationKeyIsNotPressed) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto aimInput = std::make_unique<testing::StrictMock<MockAimActivationInput>>();
    auto* aimInputPtr = aimInput.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    InferenceResult result;
    result.detections.emplace_back(InferenceDetection{
        .centerX = 400.0F,
        .centerY = 320.0F,
        .width = 20.0F,
        .height = 20.0F,
        .score = 0.8F,
        .classId = 0,
    });
    store->publish(std::move(result));

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*mousePtr, connect())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}))
        .WillOnce(testing::Return(std::unexpected(std::make_error_code(std::errc::io_error))));
    EXPECT_CALL(*aimInputPtr, isAimActivationPressed()).WillOnce(testing::Return(false));
    EXPECT_CALL(*mousePtr, move(testing::_, testing::_)).Times(0);
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

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store), std::move(aimInput));
    const auto runResult = app.run();
    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error(), std::make_error_code(std::errc::io_error));
}

TEST(AppTest, ShutdownOrderIsCaptureThenInferenceThenMouse) {
    auto mouse = std::make_unique<testing::StrictMock<MockMouseController>>();
    auto* mousePtr = mouse.get();
    auto capture = std::make_unique<testing::StrictMock<MockCaptureSource>>();
    auto* capturePtr = capture.get();
    auto inference = std::make_unique<testing::StrictMock<MockInferenceProcessor>>();
    auto* inferencePtr = inference.get();
    auto store = std::make_unique<InferenceResultStore>();

    EXPECT_CALL(*inferencePtr, start())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, start(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*capturePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*inferencePtr, poll())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
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

    App app(std::move(mouse), AppConfig{}, CaptureConfig{}, AimConfig{}, std::move(capture),
            std::move(inference), std::move(store));
    const auto result = app.run();
    EXPECT_FALSE(result.has_value());
}

} // namespace
} // namespace vf
