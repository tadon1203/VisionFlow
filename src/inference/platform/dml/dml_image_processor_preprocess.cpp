#include "inference/platform/dml/dml_image_processor_preprocess.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/inference_error.hpp"
#include "inference/platform/dml/dx_utils.hpp"

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

std::expected<winrt::com_ptr<ID3DBlob>, std::error_code> compileShader() {
    winrt::com_ptr<ID3DBlob> byteCode;
    winrt::com_ptr<ID3DBlob> errorBlob;

    constexpr UINT kCompileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr =
        D3DCompile(kPreprocessShaderSource.data(), kPreprocessShaderSource.size(), nullptr, nullptr,
                   nullptr, "main", "cs_5_0", kCompileFlags, 0, byteCode.put(), errorBlob.put());
    if (FAILED(hr)) {
        if (errorBlob != nullptr) {
            VF_ERROR("Preprocess shader compilation failed: {}",
                     static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
    }

    return byteCode;
}

} // namespace

class DmlImageProcessorPreprocess::Impl {
  public:
    struct TimestampPair {
        std::uint64_t start = 0;
        std::uint64_t end = 0;
    };

    std::expected<void, std::error_code> initialize(ID3D12Device* device,
                                                    const InitConfig& config) {
        if (device == nullptr || config.outputBytes == 0 || config.outputElementCount == 0 ||
            config.dstWidth == 0 || config.dstHeight == 0) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
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

        const auto createOutputResult = dx_utils::toError(
            dx_utils::checkD3d(d3d12Device->CreateCommittedResource(
                                   &heapProperties, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                   D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
                                   outputResource.put_void()),
                               "ID3D12Device::CreateCommittedResource(output)"),
            InferenceError::InitializationFailed);
        if (!createOutputResult) {
            return std::unexpected(createOutputResult.error());
        }

        std::array<D3D12_DESCRIPTOR_RANGE, 2> descriptorRanges{};
        descriptorRanges.at(0).RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRanges.at(0).NumDescriptors = 1;
        descriptorRanges.at(0).BaseShaderRegister = 0;
        descriptorRanges.at(0).RegisterSpace = 0;
        descriptorRanges.at(0).OffsetInDescriptorsFromTableStart = 0;

        descriptorRanges.at(1).RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorRanges.at(1).NumDescriptors = 1;
        descriptorRanges.at(1).BaseShaderRegister = 0;
        descriptorRanges.at(1).RegisterSpace = 0;
        descriptorRanges.at(1).OffsetInDescriptorsFromTableStart = 0;

        std::array<D3D12_ROOT_PARAMETER, 3> rootParameters{};
        rootParameters.at(0).ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters.at(0).Constants.ShaderRegister = 0;
        rootParameters.at(0).Constants.RegisterSpace = 0;
        rootParameters.at(0).Constants.Num32BitValues = 4;
        rootParameters.at(0).ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters.at(1).ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters.at(1).DescriptorTable.NumDescriptorRanges = 1;
        rootParameters.at(1).DescriptorTable.pDescriptorRanges = descriptorRanges.data();
        rootParameters.at(1).ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters.at(2).ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters.at(2).DescriptorTable.NumDescriptorRanges = 1;
        rootParameters.at(2).DescriptorTable.pDescriptorRanges = descriptorRanges.data() + 1;
        rootParameters.at(2).ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters = static_cast<UINT>(rootParameters.size());
        rootSigDesc.pParameters = rootParameters.data();
        rootSigDesc.NumStaticSamplers = 0;
        rootSigDesc.pStaticSamplers = nullptr;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        winrt::com_ptr<ID3DBlob> rootSigBlob;
        winrt::com_ptr<ID3DBlob> rootSigErr;
        const auto serializeResult =
            dx_utils::toError(dx_utils::checkD3d(D3D12SerializeRootSignature(
                                                     &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                     rootSigBlob.put(), rootSigErr.put()),
                                                 "D3D12SerializeRootSignature"),
                              InferenceError::InitializationFailed);
        if (!serializeResult) {
            if (rootSigErr != nullptr) {
                VF_ERROR("D3D12 root signature serialization message: {}",
                         static_cast<const char*>(rootSigErr->GetBufferPointer()));
            }
            return std::unexpected(serializeResult.error());
        }

        const auto createRootResult = dx_utils::toError(
            dx_utils::checkD3d(d3d12Device->CreateRootSignature(
                                   0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
                                   __uuidof(ID3D12RootSignature), rootSignature.put_void()),
                               "ID3D12Device::CreateRootSignature"),
            InferenceError::InitializationFailed);
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
            dx_utils::toError(dx_utils::checkD3d(d3d12Device->CreateComputePipelineState(
                                                     &psoDesc, __uuidof(ID3D12PipelineState),
                                                     pipelineState.put_void()),
                                                 "ID3D12Device::CreateComputePipelineState"),
                              InferenceError::InitializationFailed);
        if (!createPsoResult) {
            return std::unexpected(createPsoResult.error());
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 2;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NodeMask = 0;

        const auto createHeapResult =
            dx_utils::toError(dx_utils::checkD3d(d3d12Device->CreateDescriptorHeap(
                                                     &heapDesc, __uuidof(ID3D12DescriptorHeap),
                                                     descriptorHeap.put_void()),
                                                 "ID3D12Device::CreateDescriptorHeap"),
                              InferenceError::InitializationFailed);
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

        const auto createAllocatorResult = dx_utils::toError(
            dx_utils::checkD3d(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                   __uuidof(ID3D12CommandAllocator),
                                                                   commandAllocator.put_void()),
                               "ID3D12Device::CreateCommandAllocator"),
            InferenceError::InitializationFailed);
        if (!createAllocatorResult) {
            return std::unexpected(createAllocatorResult.error());
        }

        const auto createListResult = dx_utils::toError(
            dx_utils::checkD3d(d3d12Device->CreateCommandList(
                                   0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(),
                                   pipelineState.get(), __uuidof(ID3D12GraphicsCommandList),
                                   commandList.put_void()),
                               "ID3D12Device::CreateCommandList"),
            InferenceError::InitializationFailed);
        if (!createListResult) {
            return std::unexpected(createListResult.error());
        }

        const auto closeInitListResult = dx_utils::toError(
            dx_utils::checkD3d(commandList->Close(), "ID3D12GraphicsCommandList::Close(initial)"),
            InferenceError::InitializationFailed);
        if (!closeInitListResult) {
            return std::unexpected(closeInitListResult.error());
        }

        const auto createFenceResult = dx_utils::toError(
            dx_utils::checkD3d(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                        __uuidof(ID3D12Fence),
                                                        completionFenceRef.put_void()),
                               "ID3D12Device::CreateFence"),
            InferenceError::InitializationFailed);
        if (!createFenceResult) {
            return std::unexpected(createFenceResult.error());
        }

        completionEventRef.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        const auto createEventResult =
            dx_utils::toError(dx_utils::checkWin32(completionEventRef.get() != nullptr,
                                                   "CreateEventW(computeCompletionEvent)"),
                              InferenceError::InitializationFailed);
        if (!createEventResult) {
            return std::unexpected(createEventResult.error());
        }

        D3D12_QUERY_HEAP_DESC queryHeapDesc{};
        queryHeapDesc.Count = 2;
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.NodeMask = 0;

        const auto createQueryHeapResult =
            dx_utils::toError(dx_utils::checkD3d(d3d12Device->CreateQueryHeap(
                                                     &queryHeapDesc, __uuidof(ID3D12QueryHeap),
                                                     timestampQueryHeap.put_void()),
                                                 "ID3D12Device::CreateQueryHeap(timestamp)"),
                              InferenceError::InitializationFailed);
        if (!createQueryHeapResult) {
            return std::unexpected(createQueryHeapResult.error());
        }

        const D3D12_HEAP_PROPERTIES readbackHeap{
            .Type = D3D12_HEAP_TYPE_READBACK,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 1,
            .VisibleNodeMask = 1,
        };
        const D3D12_RESOURCE_DESC readbackDesc =
            dx_utils::makeRawBufferDesc(sizeof(TimestampPair), D3D12_RESOURCE_FLAG_NONE);
        const auto createReadbackResult = dx_utils::toError(
            dx_utils::checkD3d(d3d12Device->CreateCommittedResource(
                                   &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                   __uuidof(ID3D12Resource), timestampReadbackBuffer.put_void()),
                               "ID3D12Device::CreateCommittedResource(timestampReadback)"),
            InferenceError::InitializationFailed);
        if (!createReadbackResult) {
            return std::unexpected(createReadbackResult.error());
        }

        fenceValue = 0;
        timestampAvailable = false;
        initialized = true;
        commit = true;
        rollback.dismiss();
        return {};
    }

    std::expected<void, std::error_code> updateResources(ID3D12Resource* inputTexture,
                                                         DXGI_FORMAT inputFormat) {
        if (!initialized || d3d12Device == nullptr || descriptorHeap == nullptr ||
            outputResource == nullptr || inputTexture == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
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
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }

        const auto resetAllocatorResult = dx_utils::toError(
            dx_utils::checkD3d(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset"),
            InferenceError::RunFailed);
        if (!resetAllocatorResult) {
            return std::unexpected(resetAllocatorResult.error());
        }

        const auto resetListResult = dx_utils::toError(
            dx_utils::checkD3d(commandList->Reset(commandAllocator.get(), pipelineState.get()),
                               "ID3D12GraphicsCommandList::Reset"),
            InferenceError::RunFailed);
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
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
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

        if (timestampQueryHeap != nullptr) {
            commandList->EndQuery(timestampQueryHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        }

        const UINT dispatchX = (dstWidth + 7U) / 8U;
        const UINT dispatchY = (dstHeight + 7U) / 8U;
        commandList->Dispatch(dispatchX, dispatchY, 1U);

        if (timestampQueryHeap != nullptr && timestampReadbackBuffer != nullptr) {
            commandList->EndQuery(timestampQueryHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            commandList->ResolveQueryData(timestampQueryHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                          2, timestampReadbackBuffer.get(), 0);
            timestampAvailable = true;
        }

        dx_utils::transitionResource(commandList.get(), inputResource,
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                     D3D12_RESOURCE_STATE_COMMON);
        dx_utils::transitionResource(commandList.get(), outputResource.get(),
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_COMMON);
        return {};
    }

    std::expected<std::uint64_t, std::error_code>
    readLastGpuDurationUs(std::uint64_t timestampFrequency) {
        if (!initialized || !timestampAvailable || timestampReadbackBuffer == nullptr ||
            timestampFrequency == 0) {
            return std::unexpected(makeErrorCode(InferenceError::InvalidState));
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{
            .Begin = 0,
            .End = sizeof(TimestampPair),
        };
        const auto mapResult = dx_utils::toError(
            dx_utils::checkD3d(timestampReadbackBuffer->Map(0, &readRange, &mapped),
                               "ID3D12Resource::Map(timestampReadback)"),
            InferenceError::RunFailed);
        if (!mapResult) {
            return std::unexpected(mapResult.error());
        }

        const auto unmap = dx_utils::makeScopeExit([this]() {
            D3D12_RANGE writtenRange{
                .Begin = 0,
                .End = 0,
            };
            timestampReadbackBuffer->Unmap(0, &writtenRange);
        });

        if (mapped == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::RunFailed));
        }

        const auto* timestampPair = static_cast<const TimestampPair*>(mapped);
        if (timestampPair->end < timestampPair->start) {
            return std::unexpected(makeErrorCode(InferenceError::RunFailed));
        }

        const std::uint64_t deltaTicks = timestampPair->end - timestampPair->start;
        const std::uint64_t microseconds = (deltaTicks * 1'000'000ULL) / timestampFrequency;
        timestampAvailable = false;
        return microseconds;
    }

    std::expected<void, std::error_code> closeAfterRecord() const {
        if (!initialized || commandList == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }

        const auto closeResult = dx_utils::toError(
            dx_utils::checkD3d(commandList->Close(), "ID3D12GraphicsCommandList::Close"),
            InferenceError::RunFailed);
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
        timestampQueryHeap = nullptr;
        timestampReadbackBuffer = nullptr;
        d3d12Device = nullptr;
        inputResource = nullptr;
        descriptorIncrement = 0;
        outputBytesValue = 0;
        outputElementCount = 0;
        dstWidth = 0;
        dstHeight = 0;
        fenceValue = 0;
        timestampAvailable = false;
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
    winrt::com_ptr<ID3D12QueryHeap> timestampQueryHeap;
    winrt::com_ptr<ID3D12Resource> timestampReadbackBuffer;

    ID3D12Resource* inputResource = nullptr;
    dx_utils::UniqueWin32Handle completionEventRef;
    UINT descriptorIncrement = 0;
    std::size_t outputBytesValue = 0;
    std::size_t outputElementCount = 0;
    std::uint32_t dstWidth = 0;
    std::uint32_t dstHeight = 0;
    std::uint64_t fenceValue = 0;
    bool timestampAvailable = false;
};

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

DmlImageProcessorPreprocess::DmlImageProcessorPreprocess() : impl(std::make_unique<Impl>()) {}
DmlImageProcessorPreprocess::~DmlImageProcessorPreprocess() { impl->reset(); }

std::expected<void, std::error_code>
DmlImageProcessorPreprocess::initialize(ID3D12Device* device, const InitConfig& config) {
    return impl->initialize(device, config);
}

std::expected<void, std::error_code>
DmlImageProcessorPreprocess::updateResources(ID3D12Resource* inputTexture,
                                             DXGI_FORMAT inputFormat) {
    return impl->updateResources(inputTexture, inputFormat);
}

std::expected<void, std::error_code> DmlImageProcessorPreprocess::prepareForRecord() {
    return impl->prepareForRecord();
}

std::expected<void, std::error_code>
DmlImageProcessorPreprocess::recordDispatch(std::uint32_t srcWidth, std::uint32_t srcHeight) {
    return impl->recordDispatch(srcWidth, srcHeight);
}

std::expected<void, std::error_code> DmlImageProcessorPreprocess::closeAfterRecord() {
    return impl->closeAfterRecord();
}

ID3D12CommandList* DmlImageProcessorPreprocess::commandListForExecute() const {
    return impl->commandListForExecute();
}

ID3D12Fence* DmlImageProcessorPreprocess::getCompletionFence() const {
    return impl->getCompletionFence();
}
HANDLE DmlImageProcessorPreprocess::getCompletionEvent() const {
    return impl->getCompletionEvent();
}
std::uint64_t DmlImageProcessorPreprocess::nextFenceValue() { return impl->nextFenceValue(); }
std::expected<std::uint64_t, std::error_code>
DmlImageProcessorPreprocess::readLastGpuDurationUs(std::uint64_t timestampFrequency) {
    return impl->readLastGpuDurationUs(timestampFrequency);
}
ID3D12Resource* DmlImageProcessorPreprocess::getOutputResource() const {
    return impl->getOutputResource();
}
std::size_t DmlImageProcessorPreprocess::getOutputBytes() const { return impl->getOutputBytes(); }
void DmlImageProcessorPreprocess::reset() { impl->reset(); }

#else

class DmlImageProcessorPreprocess::Impl {
  public:
    [[nodiscard]] std::size_t getOutputBytes() const { return 0; }
    void reset() {}
};

DmlImageProcessorPreprocess::DmlImageProcessorPreprocess() : impl(std::make_unique<Impl>()) {}
DmlImageProcessorPreprocess::~DmlImageProcessorPreprocess() = default;

std::expected<void, std::error_code>
DmlImageProcessorPreprocess::initialize(void* device, const InitConfig& config) {
    static_cast<void>(device);
    static_cast<void>(config);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code>
DmlImageProcessorPreprocess::updateResources(void* inputTexture, int inputFormat) {
    static_cast<void>(inputTexture);
    static_cast<void>(inputFormat);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code> DmlImageProcessorPreprocess::prepareForRecord() {
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code>
DmlImageProcessorPreprocess::recordDispatch(std::uint32_t srcWidth, std::uint32_t srcHeight) {
    static_cast<void>(srcWidth);
    static_cast<void>(srcHeight);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code> DmlImageProcessorPreprocess::closeAfterRecord() {
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<std::uint64_t, std::error_code>
DmlImageProcessorPreprocess::readLastGpuDurationUs(std::uint64_t timestampFrequency) {
    static_cast<void>(timestampFrequency);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::size_t DmlImageProcessorPreprocess::getOutputBytes() const { return impl->getOutputBytes(); }
void DmlImageProcessorPreprocess::reset() { impl->reset(); }

#endif

} // namespace vf
