#include "capture/sources/winrt/capture_source_winrt.hpp"

#include <expected>
#include <system_error>

#include <gtest/gtest.h>

#include "capture/sources/winrt/winrt_frame_sink.hpp"

namespace vf {
namespace {

class DummySink final : public IWinrtFrameSink {
  public:
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override {
        static_cast<void>(texture);
        static_cast<void>(info);
    }
};

TEST(WinrtCaptureSourceTest, StopIsIdempotentWhenSourceIsIdle) {
    WinrtCaptureSource source;

    const auto first = source.stop();
    const auto second = source.stop();

    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
}

TEST(WinrtCaptureSourceTest, BindFrameSinkAcceptsNullAndNonNullSink) {
    WinrtCaptureSource source;
    DummySink sink;

    source.bindFrameSink(&sink);
    source.bindFrameSink(nullptr);

    SUCCEED();
}

} // namespace
} // namespace vf
