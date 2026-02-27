#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>

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

    explicit DmlImageProcessor(OnnxDmlSession& session);
    DmlImageProcessor(const DmlImageProcessor&) = delete;
    DmlImageProcessor(DmlImageProcessor&&) = delete;
    DmlImageProcessor& operator=(const DmlImageProcessor&) = delete;
    DmlImageProcessor& operator=(DmlImageProcessor&&) = delete;
    ~DmlImageProcessor();

#ifdef _WIN32
    [[nodiscard]] std::expected<InitializeResult, std::error_code>
    initialize(ID3D11Texture2D* sourceTexture);
    [[nodiscard]] std::expected<DispatchResult, std::error_code>
    dispatch(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue);
#else
    [[nodiscard]] std::expected<InitializeResult, std::error_code> initialize(void* sourceTexture);
    [[nodiscard]] std::expected<DispatchResult, std::error_code> dispatch(void* frameTexture,
                                                                          std::uint64_t fenceValue);
#endif

    void shutdown();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vf
