#include "capture/compute_pipeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/dx_utils.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#endif

namespace vf {

#ifdef _WIN32
// D3D12 descriptor/view structs are tagged unions in the Win32 API.
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
namespace {

constexpr std::string_view kPreprocessShaderSource = R"(
cbuffer PreprocessParams : register(b0)
{
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
};

Texture2D<float4> inputTexture : register(t0);
RWByteAddressBuffer outputTensor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= dstWidth || dispatchThreadId.y >= dstHeight) {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(dstWidth, dstHeight);
    uint srcX = min((uint)(uv.x * srcWidth), srcWidth - 1);
    uint srcY = min((uint)(uv.y * srcHeight), srcHeight - 1);

    float4 bgra = inputTexture.Load(int3(srcX, srcY, 0));

    uint planeOffset = dstWidth * dstHeight;
    uint index = dispatchThreadId.y * dstWidth + dispatchThreadId.x;

    outputTensor.Store(index * 4, asuint(bgra.z));
    outputTensor.Store((planeOffset + index) * 4, asuint(bgra.y));
    outputTensor.Store((planeOffset * 2 + index) * 4, asuint(bgra.x));
}
)";

std::expected<void, std::error_code> toError(std::expected<void, dx_utils::DxCallError> result,
                                             CaptureError errorCode) {
    if (result) {
        return {};
    }

    const dx_utils::DxCallError err = result.error();
    if (err.isWin32) {
        VF_ERROR("{} failed (Win32Error=0x{:08X})", err.apiName,
                 static_cast<std::uint32_t>(HRESULT_CODE(err.hr)));
    } else {
        VF_ERROR("{} failed (HRESULT=0x{:08X})", err.apiName, static_cast<std::uint32_t>(err.hr));
    }
    return std::unexpected(makeErrorCode(errorCode));
}

std::expected<winrt::com_ptr<ID3DBlob>, std::error_code> compileShader() {
    winrt::com_ptr<ID3DBlob> byteCode;
    winrt::com_ptr<ID3DBlob> errorBlob;

    constexpr UINT kCompileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr =
        D3DCompile(kPreprocessShaderSource.data(), kPreprocessShaderSource.size(), nullptr, nullptr,
                   nullptr, "main", "cs_5_0", kCompileFlags, 0, byteCode.put(), errorBlob.put());
    if (FAILED(hr)) {
        if (errorBlob != nullptr) {
            VF_ERROR("ComputePipeline shader compilation failed: {}",
                     static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    return byteCode;
}

} // namespace

class ComputePipeline::Impl {
  public:
    std::expected<void, std::error_code> initialize(ID3D12Device* device,
                                                    const InitConfig& config) {
        if (device == nullptr || config.outputBytes == 0 || config.outputElementCount == 0 ||
            config.dstWidth == 0 || config.dstHeight == 0) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        if (initialized) {
            const bool sameDevice = d3d12Device.get() == device;
            if (dstWidth == config.dstWidth && dstHeight == config.dstHeight &&
                outputBytesValue == config.outputBytes && sameDevice &&
                outputElementCount == config.outputElementCount) {
                return {};
            }
            reset();
        }

        bool commit = false;
        auto rollback = dx_utils::makeScopeExit([this, &commit]() {
            if (!commit) {
                reset();
            }
        });

        d3d12Device.copy_from(device);
        dstWidth = config.dstWidth;
        dstHeight = config.dstHeight;
        outputBytesValue = config.outputBytes;
        outputElementCount = config.outputElementCount;

        const auto shaderBlobResult = compileShader();
        if (!shaderBlobResult) {
            return std::unexpected(shaderBlobResult.error());
        }
        const winrt::com_ptr<ID3DBlob>& shaderBlob = shaderBlobResult.value();

        const D3D12_HEAP_PROPERTIES heapProperties = dx_utils::makeDefaultHeapProperties();
        const D3D12_RESOURCE_DESC outputDesc = dx_utils::makeRawBufferDesc(
            static_cast<UINT64>(outputBytesValue), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        const auto createOutputResult =
            toError(dx_utils::checkD3d(d3d12Device->CreateCommittedResource(
                                           &heapProperties, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                           D3D12_RESOURCE_STATE_COMMON, nullptr,
                                           __uuidof(ID3D12Resource), outputResource.put_void()),
                                       "ID3D12Device::CreateCommittedResource(output)"),
                    CaptureError::InferenceInitializationFailed);
        if (!createOutputResult) {
            return std::unexpected(createOutputResult.error());
        }

        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        std::array<D3D12_DESCRIPTOR_RANGE, 2> descriptorRanges{};
        descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRanges[0].NumDescriptors = 1;
        descriptorRanges[0].BaseShaderRegister = 0;
        descriptorRanges[0].RegisterSpace = 0;
        descriptorRanges[0].OffsetInDescriptorsFromTableStart = 0;

        descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorRanges[1].NumDescriptors = 1;
        descriptorRanges[1].BaseShaderRegister = 0;
        descriptorRanges[1].RegisterSpace = 0;
        descriptorRanges[1].OffsetInDescriptorsFromTableStart = 0;

        std::array<D3D12_ROOT_PARAMETER, 3> rootParameters{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.RegisterSpace = 0;
        rootParameters[0].Constants.Num32BitValues = 4;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRanges.data();
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRanges.data() + 1;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters = static_cast<UINT>(rootParameters.size());
        rootSigDesc.pParameters = rootParameters.data();
        rootSigDesc.NumStaticSamplers = 0;
        rootSigDesc.pStaticSamplers = nullptr;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

        winrt::com_ptr<ID3DBlob> rootSigBlob;
        winrt::com_ptr<ID3DBlob> rootSigErr;
        const auto serializeResult =
            toError(dx_utils::checkD3d(
                        D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                    rootSigBlob.put(), rootSigErr.put()),
                        "D3D12SerializeRootSignature"),
                    CaptureError::InferenceInitializationFailed);
        if (!serializeResult) {
            if (rootSigErr != nullptr) {
                VF_ERROR("D3D12 root signature serialization message: {}",
                         static_cast<const char*>(rootSigErr->GetBufferPointer()));
            }
            return std::unexpected(serializeResult.error());
        }

        const auto createRootResult = toError(
            dx_utils::checkD3d(d3d12Device->CreateRootSignature(
                                   0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                   __uuidof(ID3D12RootSignature), rootSignature.put_void()),
                               "ID3D12Device::CreateRootSignature"),
            CaptureError::InferenceInitializationFailed);
        if (!createRootResult) {
            return std::unexpected(createRootResult.error());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature.get();
        psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        const auto createPsoResult =
            toError(dx_utils::checkD3d(
                        d3d12Device->CreateComputePipelineState(
                            &psoDesc, __uuidof(ID3D12PipelineState), pipelineState.put_void()),
                        "ID3D12Device::CreateComputePipelineState"),
                    CaptureError::InferenceInitializationFailed);
        if (!createPsoResult) {
            return std::unexpected(createPsoResult.error());
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 2;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NodeMask = 0;

        const auto createHeapResult =
            toError(dx_utils::checkD3d(
                        d3d12Device->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap),
                                                          descriptorHeap.put_void()),
                        "ID3D12Device::CreateDescriptorHeap"),
                    CaptureError::InferenceInitializationFailed);
        if (!createHeapResult) {
            return std::unexpected(createHeapResult.error());
        }

        descriptorIncrement =
            d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        uavCpu.ptr += descriptorIncrement;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = static_cast<UINT>(outputElementCount);
        uavDesc.Buffer.StructureByteStride = 0;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        d3d12Device->CreateUnorderedAccessView(outputResource.get(), nullptr, &uavDesc, uavCpu);

        const auto createAllocatorResult = toError(
            dx_utils::checkD3d(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                   __uuidof(ID3D12CommandAllocator),
                                                                   commandAllocator.put_void()),
                               "ID3D12Device::CreateCommandAllocator"),
            CaptureError::InferenceInitializationFailed);
        if (!createAllocatorResult) {
            return std::unexpected(createAllocatorResult.error());
        }

        const auto createListResult = toError(
            dx_utils::checkD3d(d3d12Device->CreateCommandList(
                                   0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(),
                                   pipelineState.get(), __uuidof(ID3D12GraphicsCommandList),
                                   commandList.put_void()),
                               "ID3D12Device::CreateCommandList"),
            CaptureError::InferenceInitializationFailed);
        if (!createListResult) {
            return std::unexpected(createListResult.error());
        }

        const auto closeInitListResult = toError(
            dx_utils::checkD3d(commandList->Close(), "ID3D12GraphicsCommandList::Close(initial)"),
            CaptureError::InferenceInitializationFailed);
        if (!closeInitListResult) {
            return std::unexpected(closeInitListResult.error());
        }

        const auto createFenceResult =
            toError(dx_utils::checkD3d(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                                __uuidof(ID3D12Fence),
                                                                completionFenceRef.put_void()),
                                       "ID3D12Device::CreateFence"),
                    CaptureError::InferenceInitializationFailed);
        if (!createFenceResult) {
            return std::unexpected(createFenceResult.error());
        }

        completionEventRef.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        const auto createEventResult =
            toError(dx_utils::checkWin32(completionEventRef.get() != nullptr,
                                         "CreateEventW(computeCompletionEvent)"),
                    CaptureError::InferenceInitializationFailed);
        if (!createEventResult) {
            return std::unexpected(createEventResult.error());
        }

        fenceValue = 0;
        initialized = true;
        commit = true;
        rollback.dismiss();
        return {};
    }

    std::expected<void, std::error_code> updateResources(ID3D12Resource* inputTexture,
                                                         DXGI_FORMAT inputFormat) {
        if (!initialized || d3d12Device == nullptr || descriptorHeap == nullptr ||
            outputResource == nullptr || inputTexture == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        inputResource = inputTexture;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = inputFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0F;
        d3d12Device->CreateShaderResourceView(inputTexture, &srvDesc, srvCpu);

        return {};
    }

    std::expected<void, std::error_code> prepareForRecord() const {
        if (!initialized || commandAllocator == nullptr || commandList == nullptr ||
            pipelineState == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto resetAllocatorResult =
            toError(dx_utils::checkD3d(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset"),
                    CaptureError::InferenceRunFailed);
        if (!resetAllocatorResult) {
            return std::unexpected(resetAllocatorResult.error());
        }

        const auto resetListResult = toError(
            dx_utils::checkD3d(commandList->Reset(commandAllocator.get(), pipelineState.get()),
                               "ID3D12GraphicsCommandList::Reset"),
            CaptureError::InferenceRunFailed);
        if (!resetListResult) {
            return std::unexpected(resetListResult.error());
        }

        return {};
    }

    std::expected<void, std::error_code> recordDispatch(std::uint32_t srcWidth,
                                                        std::uint32_t srcHeight) {
        if (!initialized || commandList == nullptr || rootSignature == nullptr ||
            descriptorHeap == nullptr || inputResource == nullptr || outputResource == nullptr ||
            dstWidth == 0 || dstHeight == 0) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const std::array<std::uint32_t, 4> constants = {srcWidth, srcHeight, dstWidth, dstHeight};
        std::array<ID3D12DescriptorHeap*, 1> heaps = {descriptorHeap.get()};
        commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
        commandList->SetComputeRootSignature(rootSignature.get());
        commandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(constants.size()),
                                                  constants.data(), 0);

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = srvGpu;
        uavGpu.ptr += descriptorIncrement;
        commandList->SetComputeRootDescriptorTable(1, srvGpu);
        commandList->SetComputeRootDescriptorTable(2, uavGpu);

        dx_utils::transitionResource(commandList.get(), inputResource, D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dx_utils::transitionResource(commandList.get(), outputResource.get(),
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        const UINT dispatchX = (dstWidth + 7U) / 8U;
        const UINT dispatchY = (dstHeight + 7U) / 8U;
        commandList->Dispatch(dispatchX, dispatchY, 1U);

        dx_utils::transitionResource(commandList.get(), inputResource,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                     D3D12_RESOURCE_STATE_COMMON);
        dx_utils::transitionResource(commandList.get(), outputResource.get(),
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_COMMON);
        return {};
    }

    std::expected<void, std::error_code> closeAfterRecord() const {
        if (!initialized || commandList == nullptr) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        const auto closeResult =
            toError(dx_utils::checkD3d(commandList->Close(), "ID3D12GraphicsCommandList::Close"),
                    CaptureError::InferenceRunFailed);
        if (!closeResult) {
            return std::unexpected(closeResult.error());
        }
        return {};
    }

    [[nodiscard]] ID3D12CommandList* commandListForExecute() const { return commandList.get(); }

    [[nodiscard]] ID3D12Fence* getCompletionFence() const { return completionFenceRef.get(); }
    [[nodiscard]] HANDLE getCompletionEvent() const { return completionEventRef.get(); }

    [[nodiscard]] std::uint64_t nextFenceValue() {
        ++fenceValue;
        return fenceValue;
    }

    [[nodiscard]] ID3D12Resource* getOutputResource() const { return outputResource.get(); }

    [[nodiscard]] std::size_t getOutputBytes() const { return outputBytesValue; }

    void reset() {
        completionFenceRef = nullptr;
        commandList = nullptr;
        commandAllocator = nullptr;
        descriptorHeap = nullptr;
        pipelineState = nullptr;
        rootSignature = nullptr;
        outputResource = nullptr;
        d3d12Device = nullptr;
        inputResource = nullptr;
        descriptorIncrement = 0;
        outputBytesValue = 0;
        outputElementCount = 0;
        dstWidth = 0;
        dstHeight = 0;
        fenceValue = 0;
        initialized = false;

        completionEventRef.reset();
    }

    bool initialized = false;

    winrt::com_ptr<ID3D12Device> d3d12Device;
    winrt::com_ptr<ID3D12Resource> outputResource;
    winrt::com_ptr<ID3D12RootSignature> rootSignature;
    winrt::com_ptr<ID3D12PipelineState> pipelineState;
    winrt::com_ptr<ID3D12DescriptorHeap> descriptorHeap;
    winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
    winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
    winrt::com_ptr<ID3D12Fence> completionFenceRef;

    ID3D12Resource* inputResource = nullptr;
    dx_utils::UniqueWin32Handle completionEventRef;
    UINT descriptorIncrement = 0;
    std::size_t outputBytesValue = 0;
    std::size_t outputElementCount = 0;
    std::uint32_t dstWidth = 0;
    std::uint32_t dstHeight = 0;
    std::uint64_t fenceValue = 0;
};

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

ComputePipeline::ComputePipeline() : impl(std::make_unique<Impl>()) {}
ComputePipeline::~ComputePipeline() { impl->reset(); }

std::expected<void, std::error_code> ComputePipeline::initialize(ID3D12Device* device,
                                                                 const InitConfig& config) {
    return impl->initialize(device, config);
}

std::expected<void, std::error_code> ComputePipeline::updateResources(ID3D12Resource* inputTexture,
                                                                      DXGI_FORMAT inputFormat) {
    return impl->updateResources(inputTexture, inputFormat);
}

std::expected<void, std::error_code> ComputePipeline::prepareForRecord() {
    return impl->prepareForRecord();
}

std::expected<void, std::error_code> ComputePipeline::recordDispatch(std::uint32_t srcWidth,
                                                                     std::uint32_t srcHeight) {
    return impl->recordDispatch(srcWidth, srcHeight);
}

std::expected<void, std::error_code> ComputePipeline::closeAfterRecord() {
    return impl->closeAfterRecord();
}

ID3D12CommandList* ComputePipeline::commandListForExecute() const {
    return impl->commandListForExecute();
}

ID3D12Fence* ComputePipeline::getCompletionFence() const { return impl->getCompletionFence(); }
HANDLE ComputePipeline::getCompletionEvent() const { return impl->getCompletionEvent(); }
std::uint64_t ComputePipeline::nextFenceValue() { return impl->nextFenceValue(); }
ID3D12Resource* ComputePipeline::getOutputResource() const { return impl->getOutputResource(); }
std::size_t ComputePipeline::getOutputBytes() const { return impl->getOutputBytes(); }
void ComputePipeline::reset() { impl->reset(); }

#else

class ComputePipeline::Impl {
  public:
    [[nodiscard]] std::size_t getOutputBytes() const { return 0; }
    void reset() {}
};

ComputePipeline::ComputePipeline() : impl(std::make_unique<Impl>()) {}
ComputePipeline::~ComputePipeline() = default;

std::expected<void, std::error_code> ComputePipeline::initialize(void* device,
                                                                 const InitConfig& config) {
    static_cast<void>(device);
    static_cast<void>(config);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<void, std::error_code> ComputePipeline::updateResources(void* inputTexture,
                                                                      int inputFormat) {
    static_cast<void>(inputTexture);
    static_cast<void>(inputFormat);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<void, std::error_code> ComputePipeline::prepareForRecord() {
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<void, std::error_code> ComputePipeline::recordDispatch(std::uint32_t srcWidth,
                                                                     std::uint32_t srcHeight) {
    static_cast<void>(srcWidth);
    static_cast<void>(srcHeight);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::expected<void, std::error_code> ComputePipeline::closeAfterRecord() {
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}

std::size_t ComputePipeline::getOutputBytes() const { return impl->getOutputBytes(); }
void ComputePipeline::reset() { impl->reset(); }

#endif

} // namespace vf
