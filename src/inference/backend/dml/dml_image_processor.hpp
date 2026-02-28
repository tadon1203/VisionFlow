#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <system_error>

#include "VisionFlow/core/i_profiler.hpp"
#include "inference/engine/i_inference_image_processor.hpp"

#ifdef _WIN32
struct ID3D11Texture2D;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct IDMLDevice;
#endif

namespace vf {

class OnnxDmlSession;

class DmlImageProcessor final : public IInferenceImageProcessor {
  public:
    using InitializeResult = IInferenceImageProcessor::InitializeResult;
    using DispatchResult = IInferenceImageProcessor::DispatchResult;
    using EnqueueStatus = IInferenceImageProcessor::EnqueueStatus;

    explicit DmlImageProcessor(OnnxDmlSession& session, IProfiler* profiler = nullptr);
    DmlImageProcessor(const DmlImageProcessor&) = delete;
    DmlImageProcessor(DmlImageProcessor&&) = delete;
    DmlImageProcessor& operator=(const DmlImageProcessor&) = delete;
    DmlImageProcessor& operator=(DmlImageProcessor&&) = delete;
    ~DmlImageProcessor() noexcept;

#ifdef _WIN32
    [[nodiscard]] std::expected<InitializeResult, std::error_code>
    initialize(ID3D11Texture2D* sourceTexture) override;
    [[nodiscard]] std::expected<EnqueueStatus, std::error_code>
    enqueuePreprocess(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue) override;
    [[nodiscard]] std::expected<std::optional<DispatchResult>, std::error_code>
    tryCollectPreprocessResult() override;
#else
    [[nodiscard]] std::expected<InitializeResult, std::error_code>
    initialize(void* sourceTexture) override;
    [[nodiscard]] std::expected<EnqueueStatus, std::error_code>
    enqueuePreprocess(void* frameTexture, std::uint64_t fenceValue) override;
    [[nodiscard]] std::expected<std::optional<DispatchResult>, std::error_code>
    tryCollectPreprocessResult() override;
#endif

    void shutdown();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vf
