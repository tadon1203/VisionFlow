#include "capture/runtime/capture_runtime_winrt.hpp"

#include <expected>
#include <system_error>

#include <gtest/gtest.h>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/inference/i_inference_processor.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

namespace vf {
namespace {

class NonSinkInferenceProcessor final : public IInferenceProcessor {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> stop() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> poll() override { return {}; }
};

class SinkInferenceProcessor final : public IInferenceProcessor, public IWinrtFrameSink {
  public:
    [[nodiscard]] std::expected<void, std::error_code> start() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> stop() override { return {}; }
    [[nodiscard]] std::expected<void, std::error_code> poll() override { return {}; }
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override {
        static_cast<void>(texture);
        static_cast<void>(info);
    }
};

TEST(WinrtCaptureRuntimeTest, AttachInferenceProcessorFailsWhenSinkInterfaceIsMissing) {
    WinrtCaptureRuntime runtime;
    NonSinkInferenceProcessor processor;

    const auto result = runtime.attachInferenceProcessor(processor);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(CaptureError::InferenceInterfaceNotSupported));
}

TEST(WinrtCaptureRuntimeTest, StartFailsWithInvalidStateWhenSinkIsNotAttached) {
    WinrtCaptureRuntime runtime;

    const auto result = runtime.start(CaptureConfig{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(CaptureError::InvalidState));
}

TEST(WinrtCaptureRuntimeTest, AttachInferenceProcessorSucceedsWhenSinkInterfaceExists) {
    WinrtCaptureRuntime runtime;
    SinkInferenceProcessor processor;

    const auto result = runtime.attachInferenceProcessor(processor);
    EXPECT_TRUE(result.has_value());
}

TEST(WinrtCaptureRuntimeTest, StopIsIdempotentWhenRuntimeIsIdle) {
    WinrtCaptureRuntime runtime;

    const auto first = runtime.stop();
    const auto second = runtime.stop();

    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
}

TEST(WinrtCaptureRuntimeTest, PollSucceedsWhenNoFaultIsPresent) {
    WinrtCaptureRuntime runtime;

    const auto result = runtime.poll();
    EXPECT_TRUE(result.has_value());
}

} // namespace
} // namespace vf
