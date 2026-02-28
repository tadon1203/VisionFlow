#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>

#ifdef _WIN32
struct ID3D11Texture2D;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct IDMLDevice;
#endif

namespace vf {

class IInferenceImageProcessor {
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

    IInferenceImageProcessor() = default;
    IInferenceImageProcessor(const IInferenceImageProcessor&) = delete;
    IInferenceImageProcessor(IInferenceImageProcessor&&) = delete;
    IInferenceImageProcessor& operator=(const IInferenceImageProcessor&) = delete;
    IInferenceImageProcessor& operator=(IInferenceImageProcessor&&) = delete;
    virtual ~IInferenceImageProcessor() = default;

#ifdef _WIN32
    [[nodiscard]] virtual std::expected<InitializeResult, std::error_code>
    initialize(ID3D11Texture2D* sourceTexture) = 0;
    [[nodiscard]] virtual std::expected<EnqueueStatus, std::error_code>
    enqueuePreprocess(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue) = 0;
#else
    [[nodiscard]] virtual std::expected<InitializeResult, std::error_code>
    initialize(void* sourceTexture) = 0;
    [[nodiscard]] virtual std::expected<EnqueueStatus, std::error_code>
    enqueuePreprocess(void* frameTexture, std::uint64_t fenceValue) = 0;
#endif
    [[nodiscard]] virtual std::expected<std::optional<DispatchResult>, std::error_code>
    tryCollectPreprocessResult() = 0;
};

} // namespace vf
