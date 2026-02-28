#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <system_error>

#include "VisionFlow/core/i_profiler.hpp"

#ifdef _WIN32
struct ID3D11Texture2D;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct IDMLDevice;
#endif

namespace vf {

class OnnxDmlSession;

class DmlImageProcessor {
  public:
    struct InitializeResult {
#ifdef _WIN32
        IDMLDevice* dmlDevice = nullptr;
        ID3D12CommandQueue* commandQueue = nullptr;
#else
        void* dmlDevice = nullptr;
        void* commandQueue = nullptr;
#endif
    };

    struct DispatchResult {
#ifdef _WIN32
        ID3D12Resource* outputResource = nullptr;
#else
        void* outputResource = nullptr;
#endif
        std::size_t outputBytes = 0;
    };

    enum class EnqueueStatus : std::uint8_t {
        Submitted,
        SkippedBusy,
    };

    explicit DmlImageProcessor(OnnxDmlSession& session, IProfiler* profiler = nullptr);
    DmlImageProcessor(const DmlImageProcessor&) = delete;
    DmlImageProcessor(DmlImageProcessor&&) = delete;
    DmlImageProcessor& operator=(const DmlImageProcessor&) = delete;
    DmlImageProcessor& operator=(DmlImageProcessor&&) = delete;
    ~DmlImageProcessor() noexcept;

#ifdef _WIN32
    [[nodiscard]] std::expected<InitializeResult, std::error_code>
    initialize(ID3D11Texture2D* sourceTexture);
    [[nodiscard]] std::expected<EnqueueStatus, std::error_code>
    enqueuePreprocess(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue);
    [[nodiscard]] std::expected<std::optional<DispatchResult>, std::error_code>
    tryCollectPreprocessResult();
#else
    [[nodiscard]] std::expected<InitializeResult, std::error_code> initialize(void* sourceTexture);
    [[nodiscard]] std::expected<EnqueueStatus, std::error_code>
    enqueuePreprocess(void* frameTexture, std::uint64_t fenceValue);
    [[nodiscard]] std::expected<std::optional<DispatchResult>, std::error_code>
    tryCollectPreprocessResult();
#endif

    void shutdown();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vf
