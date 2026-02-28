#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>

#ifdef _WIN32
struct D3D11_TEXTURE2D_DESC;
struct ID3D11Texture2D;
struct ID3D12CommandQueue;
struct ID3D12Device;
struct ID3D12Resource;
struct IDMLDevice;
#endif

namespace vf {

struct DmlInteropUpdateResult {
    std::uint64_t generationId = 0;
    bool reinitialized = false;
    bool sourceChanged = false;
#ifdef _WIN32
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* commandQueue = nullptr;
    IDMLDevice* dmlDevice = nullptr;
    ID3D12Resource* sharedInputTexture = nullptr;
#else
    void* device = nullptr;
    void* commandQueue = nullptr;
    void* dmlDevice = nullptr;
    void* sharedInputTexture = nullptr;
#endif
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    std::uint32_t sourceFormat = 0;
};

class DmlImageProcessorInterop {
  public:
    DmlImageProcessorInterop();
    DmlImageProcessorInterop(const DmlImageProcessorInterop&) = delete;
    DmlImageProcessorInterop(DmlImageProcessorInterop&&) = delete;
    DmlImageProcessorInterop& operator=(const DmlImageProcessorInterop&) = delete;
    DmlImageProcessorInterop& operator=(DmlImageProcessorInterop&&) = delete;
    ~DmlImageProcessorInterop();

#ifdef _WIN32
    [[nodiscard]] std::expected<DmlInteropUpdateResult, std::error_code>
    initializeOrUpdate(ID3D11Texture2D* sourceTexture);
    [[nodiscard]] std::expected<std::uint64_t, std::error_code>
    copyAndSignal(ID3D11Texture2D* frameTexture, std::uint64_t requestedFenceValue);
    [[nodiscard]] std::expected<void, std::error_code> waitOnQueue(std::uint64_t fenceValue);

    [[nodiscard]] ID3D12Device* getD3d12Device() const;
    [[nodiscard]] ID3D12CommandQueue* getD3d12Queue() const;
    [[nodiscard]] IDMLDevice* getDmlDevice() const;
    [[nodiscard]] ID3D12Resource* getSharedInputTextureD3d12() const;
    [[nodiscard]] const D3D11_TEXTURE2D_DESC& getSourceDesc() const;
#else
    [[nodiscard]] std::expected<DmlInteropUpdateResult, std::error_code>
    initializeOrUpdate(void* sourceTexture);
    [[nodiscard]] std::expected<std::uint64_t, std::error_code>
    copyAndSignal(void* frameTexture, std::uint64_t requestedFenceValue);
    [[nodiscard]] std::expected<void, std::error_code> waitOnQueue(std::uint64_t fenceValue);
#endif

    void reset();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vf
