#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "capture/common/inference_result.hpp"

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
#include <onnxruntime/core/providers/dml/dml_provider_factory.h>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

#include <winrt/base.h>
#endif

#ifdef _WIN32
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct IDMLDevice;
#endif

namespace vf {

class OnnxDmlSession {
  public:
    struct ModelMetadata {
        std::string inputName;
        std::vector<std::string> outputNames;
        std::vector<int64_t> inputShape;
        std::uint32_t inputChannels = 0;
        std::uint32_t inputHeight = 0;
        std::uint32_t inputWidth = 0;
        std::size_t inputElementCount = 0;
        std::size_t inputTensorBytes = 0;
    };

    explicit OnnxDmlSession(std::filesystem::path modelPath);
    OnnxDmlSession(const OnnxDmlSession&) = delete;
    OnnxDmlSession(OnnxDmlSession&&) = delete;
    OnnxDmlSession& operator=(const OnnxDmlSession&) = delete;
    OnnxDmlSession& operator=(OnnxDmlSession&&) = delete;
    ~OnnxDmlSession() noexcept;

    [[nodiscard]] static std::expected<ModelMetadata, std::error_code>
    createModelMetadata(std::string inputName, std::vector<int64_t> inputShape,
                        std::vector<std::string> outputNames);

#ifdef _WIN32
    [[nodiscard]] std::expected<void, std::error_code>
    start(IDMLDevice* dmlDevice, ID3D12CommandQueue* commandQueue, std::uint64_t interopGeneration);
    [[nodiscard]] std::expected<void, std::error_code> start(IDMLDevice* dmlDevice,
                                                             ID3D12CommandQueue* commandQueue);
#else
    [[nodiscard]] std::expected<void, std::error_code> start(void* dmlDevice, void* commandQueue,
                                                             std::uint64_t interopGeneration);
    [[nodiscard]] std::expected<void, std::error_code> start(void* dmlDevice, void* commandQueue);
#endif
    [[nodiscard]] std::expected<void, std::error_code> stop();

    [[nodiscard]] const ModelMetadata& metadata() const;

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    [[nodiscard]] std::expected<InferenceResult, std::error_code>
    runWithGpuInput(std::int64_t frameTimestamp100ns, ID3D12Resource* resource,
                    std::size_t resourceBytes);
#endif

  private:
    [[nodiscard]] std::filesystem::path resolveModelPath() const;

    std::filesystem::path modelPath;
    ModelMetadata modelMetadata;
    bool running = false;

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    const OrtDmlApi* dmlApi = nullptr;
    std::unique_ptr<Ort::Env> ortEnv;
    std::unique_ptr<Ort::SessionOptions> sessionOptions;
    std::unique_ptr<Ort::Session> session;
    std::unique_ptr<Ort::MemoryInfo> dmlMemoryInfo;
    std::unique_ptr<Ort::MemoryInfo> cpuMemoryInfo;
    void* inputAllocation = nullptr;

    winrt::com_ptr<IDMLDevice> dmlDevice;
    winrt::com_ptr<ID3D12CommandQueue> d3d12Queue;
    winrt::com_ptr<ID3D12Device> d3d12Device;
    ID3D12Resource* boundInputResource = nullptr;
    std::uint64_t boundInteropGeneration = 0;
#endif
};

} // namespace vf
