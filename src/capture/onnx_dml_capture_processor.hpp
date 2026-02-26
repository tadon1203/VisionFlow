#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <system_error>
#include <thread>

#include "VisionFlow/core/config.hpp"
#include "capture/i_capture_processor.hpp"
#include "capture/inference_result_store.hpp"

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
#include <DirectML.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>
#endif

namespace vf {

class OnnxDmlSession;

class OnnxDmlCaptureProcessor final : public ICaptureProcessor {
  public:
    explicit OnnxDmlCaptureProcessor(InferenceConfig config);
    OnnxDmlCaptureProcessor(const OnnxDmlCaptureProcessor&) = delete;
    OnnxDmlCaptureProcessor(OnnxDmlCaptureProcessor&&) = delete;
    OnnxDmlCaptureProcessor& operator=(const OnnxDmlCaptureProcessor&) = delete;
    OnnxDmlCaptureProcessor& operator=(OnnxDmlCaptureProcessor&&) = delete;
    ~OnnxDmlCaptureProcessor() override;

    [[nodiscard]] std::expected<void, std::error_code> start() override;
    [[nodiscard]] std::expected<void, std::error_code> stop() override;

#ifdef _WIN32
    void onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) override;
#else
    void onFrame(void* texture, const CaptureFrameInfo& info) override;
#endif

  private:
    enum class ProcessorState : std::uint8_t {
        Idle,
        Starting,
        Running,
        Stopping,
        Fault,
    };

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    struct PreprocessConstants {
        std::uint32_t srcWidth = 0;
        std::uint32_t srcHeight = 0;
        std::uint32_t dstWidth = 0;
        std::uint32_t dstHeight = 0;
    };

    [[nodiscard]] std::expected<void, std::error_code>
    initializeGpuResources(ID3D11Texture2D* sourceTexture);
    [[nodiscard]] std::expected<void, std::error_code>
    initializeD3d11Interop(ID3D11Texture2D* sourceTexture);
    [[nodiscard]] std::expected<void, std::error_code> initializeD3d12AndDmlDeviceIfNeeded();
    [[nodiscard]] std::expected<void, std::error_code> startOnnxSession();
    [[nodiscard]] std::expected<void, std::error_code> createSharedInputTextureResources();
    [[nodiscard]] std::expected<void, std::error_code> initializePreprocessResources();
    [[nodiscard]] std::expected<void, std::error_code>
    dispatchPreprocess(std::uint64_t inputReadyFenceValue);
    void releaseGpuResources();
#endif

    void inferenceLoop(const std::stop_token& stopToken);

    InferenceConfig config;
    std::mutex stateMutex;
    ProcessorState state = ProcessorState::Idle;

    std::jthread workerThread;

    std::mutex frameMutex;
    std::condition_variable frameCv;
    bool hasPendingFrame = false;
    std::int64_t pendingTimestamp100ns = 0;
    std::uint64_t pendingFrameFenceValue = 0;
    std::size_t droppedFrames = 0;
    std::chrono::steady_clock::time_point lastDropLogTime;

    std::unique_ptr<OnnxDmlSession> session;
    InferenceResultStore resultStore;

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    bool gpuReady = false;
    bool gpuFault = false;

    std::mutex gpuMutex;

    winrt::com_ptr<ID3D11Device> d3d11Device;
    winrt::com_ptr<ID3D11DeviceContext> d3d11Context;
    winrt::com_ptr<ID3D11Device5> d3d11Device5;
    winrt::com_ptr<ID3D11DeviceContext4> d3d11Context4;
    winrt::com_ptr<ID3D12Device> d3d12Device;
    winrt::com_ptr<ID3D12CommandQueue> d3d12Queue;
    winrt::com_ptr<IDMLDevice> dmlDevice;

    winrt::com_ptr<ID3D11Texture2D> sharedInputTextureD3d11;
    winrt::com_ptr<ID3D12Resource> sharedInputTextureD3d12;
    D3D11_TEXTURE2D_DESC latestFrameDesc{};

    winrt::com_ptr<ID3D12Resource> preprocessOutputResourceD3d12;
    winrt::com_ptr<ID3D12RootSignature> preprocessRootSignature;
    winrt::com_ptr<ID3D12PipelineState> preprocessPipelineState;
    winrt::com_ptr<ID3D12DescriptorHeap> preprocessDescriptorHeap;
    winrt::com_ptr<ID3D12CommandAllocator> preprocessCommandAllocator;
    winrt::com_ptr<ID3D12GraphicsCommandList> preprocessCommandList;
    winrt::com_ptr<ID3D12Fence> preprocessFence;
    winrt::com_ptr<ID3D11Fence> interopFenceD3d11;
    winrt::com_ptr<ID3D12Fence> interopFenceD3d12;
    UINT preprocessDescriptorIncrement = 0;
    std::uint64_t preprocessFenceValue = 0;
    std::uint64_t interopFenceValue = 0;
    HANDLE preprocessFenceEvent = nullptr;

    HMODULE directmlModule = nullptr;
    HANDLE preprocessSharedHandle = nullptr;
    HANDLE interopFenceSharedHandle = nullptr;
#endif
};

} // namespace vf
