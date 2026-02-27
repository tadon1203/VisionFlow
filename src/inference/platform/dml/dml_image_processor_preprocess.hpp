#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#include <dxgiformat.h>

struct ID3D12CommandList;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12Resource;
#endif

namespace vf {

class DmlImageProcessorPreprocess {
  public:
    struct InitConfig {
        std::uint32_t dstWidth = 0;
        std::uint32_t dstHeight = 0;
        std::size_t outputBytes = 0;
        std::size_t outputElementCount = 0;
    };

    DmlImageProcessorPreprocess();
    DmlImageProcessorPreprocess(const DmlImageProcessorPreprocess&) = delete;
    DmlImageProcessorPreprocess(DmlImageProcessorPreprocess&&) = delete;
    DmlImageProcessorPreprocess& operator=(const DmlImageProcessorPreprocess&) = delete;
    DmlImageProcessorPreprocess& operator=(DmlImageProcessorPreprocess&&) = delete;
    ~DmlImageProcessorPreprocess();

#ifdef _WIN32
    [[nodiscard]] std::expected<void, std::error_code> initialize(ID3D12Device* device,
                                                                  const InitConfig& config);
    [[nodiscard]] std::expected<void, std::error_code> updateResources(ID3D12Resource* inputTexture,
                                                                       DXGI_FORMAT inputFormat);
    [[nodiscard]] std::expected<void, std::error_code> prepareForRecord();
    [[nodiscard]] std::expected<void, std::error_code> recordDispatch(std::uint32_t srcWidth,
                                                                      std::uint32_t srcHeight);
    [[nodiscard]] std::expected<void, std::error_code> closeAfterRecord();

    [[nodiscard]] ID3D12CommandList* commandListForExecute() const;
    [[nodiscard]] ID3D12Fence* getCompletionFence() const;
    [[nodiscard]] HANDLE getCompletionEvent() const;
    [[nodiscard]] std::uint64_t nextFenceValue();

    [[nodiscard]] ID3D12Resource* getOutputResource() const;
#else
    [[nodiscard]] std::expected<void, std::error_code> initialize(void* device,
                                                                  const InitConfig& config);
    [[nodiscard]] std::expected<void, std::error_code> updateResources(void* inputTexture,
                                                                       int inputFormat);
    [[nodiscard]] std::expected<void, std::error_code> prepareForRecord();
    [[nodiscard]] std::expected<void, std::error_code> recordDispatch(std::uint32_t srcWidth,
                                                                      std::uint32_t srcHeight);
    [[nodiscard]] std::expected<void, std::error_code> closeAfterRecord();
#endif

    [[nodiscard]] std::size_t getOutputBytes() const;
    void reset();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vf
