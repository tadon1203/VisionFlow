#include "capture/onnx_dml_capture_processor.hpp"

#include <array>
#include <chrono>
#include <expected>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/onnx_dml_session.hpp"

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#endif

namespace vf {

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
// Direct3D descriptor/resource barrier structs are tagged unions in the Win32 API.
// Accessing the active union members is required in this translation unit.
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

std::expected<winrt::com_ptr<ID3DBlob>, std::error_code> compilePreprocessShaderBlob() {
    winrt::com_ptr<ID3DBlob> byteCode;
    winrt::com_ptr<ID3DBlob> errorBlob;

    constexpr UINT kCompileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr =
        D3DCompile(kPreprocessShaderSource.data(), kPreprocessShaderSource.size(), nullptr, nullptr,
                   nullptr, "main", "cs_5_0", kCompileFlags, 0, byteCode.put(), errorBlob.put());
    if (FAILED(hr)) {
        if (errorBlob.get() != nullptr) {
            VF_ERROR("OnnxDmlCaptureProcessor shader compilation failed: {}",
                     static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    return byteCode;
}

std::expected<void, std::error_code> checkD3dCall(HRESULT hr, std::string_view apiName) {
    if (SUCCEEDED(hr)) {
        return {};
    }

    VF_ERROR("{} failed (HRESULT=0x{:08X})", apiName, static_cast<std::uint32_t>(hr));
    return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
}

std::expected<void, std::error_code> checkWin32Call(bool result, std::string_view apiName) {
    if (result) {
        return {};
    }

    const DWORD lastError = GetLastError();
    VF_ERROR("{} failed (Win32Error=0x{:08X})", apiName, static_cast<std::uint32_t>(lastError));
    return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
}

std::expected<void, std::error_code> loadDirectmlModule(HMODULE& directmlModule) {
    if (directmlModule != nullptr) {
        return {};
    }

    directmlModule = LoadLibraryW(L"DirectML.dll");
    const std::expected<void, std::error_code> loadResult =
        checkWin32Call(directmlModule != nullptr, "LoadLibraryW(DirectML.dll)");
    if (!loadResult) {
        return std::unexpected(loadResult.error());
    }

    return {};
}

std::expected<void, std::error_code> createDmlDevice(ID3D12Device* d3d12Device,
                                                     HMODULE directmlModule,
                                                     winrt::com_ptr<IDMLDevice>& dmlDevice) {
    if (d3d12Device == nullptr || directmlModule == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    using DmlCreateDevice1Fn =
        HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, DML_FEATURE_LEVEL, REFIID, void**);
    auto* createDevice1 =
        reinterpret_cast<DmlCreateDevice1Fn>(GetProcAddress(directmlModule, "DMLCreateDevice1"));
    if (createDevice1 == nullptr) {
        VF_ERROR("GetProcAddress(DMLCreateDevice1) failed (Win32Error=0x{:08X})",
                 static_cast<std::uint32_t>(GetLastError()));
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    const std::expected<void, std::error_code> createResult =
        checkD3dCall(createDevice1(d3d12Device, DML_CREATE_DEVICE_FLAG_NONE, DML_FEATURE_LEVEL_1_0,
                                   __uuidof(IDMLDevice), dmlDevice.put_void()),
                     "DMLCreateDevice1");
    if (!createResult) {
        return std::unexpected(createResult.error());
    }

    return {};
}

} // namespace
#endif

OnnxDmlCaptureProcessor::OnnxDmlCaptureProcessor(InferenceConfig config)
    : config(std::move(config)), lastDropLogTime(std::chrono::steady_clock::now()) {}

OnnxDmlCaptureProcessor::~OnnxDmlCaptureProcessor() {
    const std::expected<void, std::error_code> result = stop();
    if (!result) {
        VF_WARN("OnnxDmlCaptureProcessor stop during destruction failed: {}",
                result.error().message());
    }
}

std::expected<void, std::error_code> OnnxDmlCaptureProcessor::start() {
#if !defined(_WIN32) || !defined(VF_HAS_ONNXRUNTIME_DML) || !VF_HAS_ONNXRUNTIME_DML
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(stateMutex);
        if (state == ProcessorState::Running) {
            return {};
        }
        if (state == ProcessorState::Starting || state == ProcessorState::Stopping) {
            return std::unexpected(makeErrorCode(CaptureError::InvalidState));
        }
        state = ProcessorState::Starting;
    }

    session = std::make_unique<OnnxDmlSession>(config.modelPath);

    {
        std::scoped_lock lock(frameMutex);
        hasPendingFrame = false;
        pendingTimestamp100ns = 0;
        pendingFrameFenceValue = 0;
        droppedFrames = 0;
        lastDropLogTime = std::chrono::steady_clock::now();
    }

    gpuReady = false;
    gpuFault = false;
    workerThread =
        std::jthread([this](const std::stop_token& stopToken) { inferenceLoop(stopToken); });

    {
        std::scoped_lock lock(stateMutex);
        state = ProcessorState::Running;
    }

    VF_INFO("OnnxDmlCaptureProcessor started");
    return {};
#endif
}

std::expected<void, std::error_code> OnnxDmlCaptureProcessor::stop() {
#if !defined(_WIN32) || !defined(VF_HAS_ONNXRUNTIME_DML) || !VF_HAS_ONNXRUNTIME_DML
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(stateMutex);
        if (state == ProcessorState::Idle) {
            return {};
        }
        state = ProcessorState::Stopping;
    }

    if (workerThread.joinable()) {
        workerThread.request_stop();
        frameCv.notify_all();
        workerThread.join();
    }

    {
        std::scoped_lock gpuLock(gpuMutex);
        releaseGpuResources();
    }

    std::error_code stopError;
    if (session != nullptr) {
        const std::expected<void, std::error_code> sessionStopResult = session->stop();
        if (!sessionStopResult) {
            stopError = sessionStopResult.error();
        }
    }

    session.reset();
    dmlDevice = nullptr;
    d3d12Queue = nullptr;
    d3d12Device = nullptr;
    if (directmlModule != nullptr) {
        const BOOL freeResult = FreeLibrary(directmlModule);
        if (freeResult == FALSE) {
            VF_WARN("OnnxDmlCaptureProcessor failed to free DirectML module");
        }
        directmlModule = nullptr;
    }

    {
        std::scoped_lock lock(stateMutex);
        state = stopError ? ProcessorState::Fault : ProcessorState::Idle;
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    VF_INFO("OnnxDmlCaptureProcessor stopped");
    return {};
#endif
}

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML

std::expected<void, std::error_code> OnnxDmlCaptureProcessor::initializePreprocessResources() {
    if (session == nullptr || d3d12Device.get() == nullptr ||
        sharedInputTextureD3d12.get() == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    const OnnxDmlSession::ModelMetadata& modelMetadata = session->metadata();
    const auto shaderBlobResult = compilePreprocessShaderBlob();
    if (!shaderBlobResult) {
        return std::unexpected(shaderBlobResult.error());
    }
    const winrt::com_ptr<ID3DBlob>& shaderBlob = shaderBlobResult.value();

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC outputResourceDesc{};
    outputResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    outputResourceDesc.Alignment = 0;
    outputResourceDesc.Width = static_cast<UINT64>(modelMetadata.inputTensorBytes);
    outputResourceDesc.Height = 1;
    outputResourceDesc.DepthOrArraySize = 1;
    outputResourceDesc.MipLevels = 1;
    outputResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    outputResourceDesc.SampleDesc.Count = 1;
    outputResourceDesc.SampleDesc.Quality = 0;
    outputResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    outputResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    const std::expected<void, std::error_code> createOutputResourceResult = checkD3dCall(
        d3d12Device->CreateCommittedResource(
            &heapProperties, D3D12_HEAP_FLAG_NONE, &outputResourceDesc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, __uuidof(ID3D12Resource), preprocessOutputResourceD3d12.put_void()),
        "ID3D12Device::CreateCommittedResource(output)");
    if (!createOutputResourceResult) {
        return std::unexpected(createOutputResourceResult.error());
    }

    D3D12_DESCRIPTOR_RANGE descriptorRanges[2]{};
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

    // D3D12_ROOT_PARAMETER is a tagged union in the Win32 API. Union member access is required.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    D3D12_ROOT_PARAMETER rootParameters[3]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.RegisterSpace = 0;
    rootParameters[0].Constants.Num32BitValues = 4;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRanges[0];
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &descriptorRanges[1];
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.NumParameters = static_cast<UINT>(std::size(rootParameters));
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    winrt::com_ptr<ID3DBlob> rootSignatureBlob;
    winrt::com_ptr<ID3DBlob> errorBlob;
    const std::expected<void, std::error_code> serializeRootSignatureResult =
        checkD3dCall(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                 rootSignatureBlob.put(), errorBlob.put()),
                     "D3D12SerializeRootSignature");
    if (!serializeRootSignatureResult) {
        if (errorBlob.get() != nullptr) {
            VF_ERROR("D3D12 root signature serialization message: {}",
                     static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return std::unexpected(serializeRootSignatureResult.error());
    }

    const std::expected<void, std::error_code> createRootSignatureResult =
        checkD3dCall(d3d12Device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
                                                      rootSignatureBlob->GetBufferSize(),
                                                      __uuidof(ID3D12RootSignature),
                                                      preprocessRootSignature.put_void()),
                     "ID3D12Device::CreateRootSignature");
    if (!createRootSignatureResult) {
        return std::unexpected(createRootSignatureResult.error());
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc{};
    pipelineStateDesc.pRootSignature = preprocessRootSignature.get();
    pipelineStateDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
    pipelineStateDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
    pipelineStateDesc.NodeMask = 0;
    pipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const std::expected<void, std::error_code> createPipelineResult = checkD3dCall(
        d3d12Device->CreateComputePipelineState(&pipelineStateDesc, __uuidof(ID3D12PipelineState),
                                                preprocessPipelineState.put_void()),
        "ID3D12Device::CreateComputePipelineState");
    if (!createPipelineResult) {
        return std::unexpected(createPipelineResult.error());
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.NumDescriptors = 2;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptorHeapDesc.NodeMask = 0;
    const std::expected<void, std::error_code> createDescriptorHeapResult = checkD3dCall(
        d3d12Device->CreateDescriptorHeap(&descriptorHeapDesc, __uuidof(ID3D12DescriptorHeap),
                                          preprocessDescriptorHeap.put_void()),
        "ID3D12Device::CreateDescriptorHeap");
    if (!createDescriptorHeapResult) {
        return std::unexpected(createDescriptorHeapResult.error());
    }
    preprocessDescriptorIncrement =
        d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle =
        preprocessDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle = srvCpuHandle;
    uavCpuHandle.ptr += preprocessDescriptorIncrement;

    // D3D12 *_VIEW_DESC structs are tagged unions in the Win32 API.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = latestFrameDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0F;
    d3d12Device->CreateShaderResourceView(sharedInputTextureD3d12.get(), &srvDesc, srvCpuHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = static_cast<UINT>(modelMetadata.inputElementCount);
    uavDesc.Buffer.StructureByteStride = 0;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    d3d12Device->CreateUnorderedAccessView(preprocessOutputResourceD3d12.get(), nullptr, &uavDesc,
                                           uavCpuHandle);

    const std::expected<void, std::error_code> createCommandAllocatorResult =
        checkD3dCall(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         __uuidof(ID3D12CommandAllocator),
                                                         preprocessCommandAllocator.put_void()),
                     "ID3D12Device::CreateCommandAllocator");
    if (!createCommandAllocatorResult) {
        return std::unexpected(createCommandAllocatorResult.error());
    }

    const std::expected<void, std::error_code> createCommandListResult =
        checkD3dCall(d3d12Device->CreateCommandList(
                         0, D3D12_COMMAND_LIST_TYPE_DIRECT, preprocessCommandAllocator.get(),
                         preprocessPipelineState.get(), __uuidof(ID3D12GraphicsCommandList),
                         preprocessCommandList.put_void()),
                     "ID3D12Device::CreateCommandList");
    if (!createCommandListResult) {
        return std::unexpected(createCommandListResult.error());
    }
    const std::expected<void, std::error_code> closeCommandListResult =
        checkD3dCall(preprocessCommandList->Close(), "ID3D12GraphicsCommandList::Close(initial)");
    if (!closeCommandListResult) {
        return std::unexpected(closeCommandListResult.error());
    }

    const std::expected<void, std::error_code> createFenceResult =
        checkD3dCall(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                              preprocessFence.put_void()),
                     "ID3D12Device::CreateFence");
    if (!createFenceResult) {
        return std::unexpected(createFenceResult.error());
    }

    preprocessFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const std::expected<void, std::error_code> createFenceEventResult =
        checkWin32Call(preprocessFenceEvent != nullptr, "CreateEventW(preprocessFenceEvent)");
    if (!createFenceEventResult) {
        return std::unexpected(createFenceEventResult.error());
    }
    preprocessFenceValue = 0;

    const std::expected<void, std::error_code> inputBindingResult =
        session->initializeGpuInputFromResource(preprocessOutputResourceD3d12.get(),
                                                modelMetadata.inputTensorBytes);
    if (!inputBindingResult) {
        return std::unexpected(inputBindingResult.error());
    }

    return {};
}

std::expected<void, std::error_code>
OnnxDmlCaptureProcessor::initializeGpuResources(ID3D11Texture2D* sourceTexture) {
    if (gpuReady) {
        return {};
    }
    const std::expected<void, std::error_code> d3d11InitResult =
        initializeD3d11Interop(sourceTexture);
    if (!d3d11InitResult) {
        return std::unexpected(d3d11InitResult.error());
    }

    const std::expected<void, std::error_code> d3d12InitResult =
        initializeD3d12AndDmlDeviceIfNeeded();
    if (!d3d12InitResult) {
        return std::unexpected(d3d12InitResult.error());
    }

    const std::expected<void, std::error_code> sessionStartResult = startOnnxSession();
    if (!sessionStartResult) {
        return std::unexpected(sessionStartResult.error());
    }

    const std::expected<void, std::error_code> interopInitResult =
        createSharedInputTextureResources();
    if (!interopInitResult) {
        return std::unexpected(interopInitResult.error());
    }

    VF_INFO("OnnxDmlCaptureProcessor GPU pipeline is ready");
    VF_DEBUG(
        "OnnxDmlCaptureProcessor initialized D3D11 shared input texture and D3D11/D3D12 fence");

    gpuReady = true;
    return {};
}

std::expected<void, std::error_code>
OnnxDmlCaptureProcessor::initializeD3d11Interop(ID3D11Texture2D* sourceTexture) {
    if (sourceTexture == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    sourceTexture->GetDevice(d3d11Device.put());
    if (d3d11Device.get() == nullptr) {
        VF_ERROR("ID3D11Texture2D::GetDevice returned null device");
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    d3d11Device->GetImmediateContext(d3d11Context.put());
    if (d3d11Context.get() == nullptr) {
        VF_ERROR("ID3D11Device::GetImmediateContext returned null context");
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    const std::expected<void, std::error_code> queryDevice5Result =
        checkD3dCall(d3d11Device->QueryInterface(__uuidof(ID3D11Device5), d3d11Device5.put_void()),
                     "ID3D11Device::QueryInterface(ID3D11Device5)");
    if (!queryDevice5Result) {
        VF_ERROR("D3D11 fence interop requires ID3D11Device5 support");
        return std::unexpected(queryDevice5Result.error());
    }

    const std::expected<void, std::error_code> queryContext4Result = checkD3dCall(
        d3d11Context->QueryInterface(__uuidof(ID3D11DeviceContext4), d3d11Context4.put_void()),
        "ID3D11DeviceContext::QueryInterface(ID3D11DeviceContext4)");
    if (!queryContext4Result) {
        VF_ERROR("D3D11 fence interop requires ID3D11DeviceContext4 support");
        return std::unexpected(queryContext4Result.error());
    }

    sourceTexture->GetDesc(&latestFrameDesc);
    VF_DEBUG(
        "OnnxDmlCaptureProcessor source texture desc: {}x{} format={} mipLevels={} arraySize={} "
        "sampleCount={}",
        latestFrameDesc.Width, latestFrameDesc.Height,
        static_cast<std::uint32_t>(latestFrameDesc.Format), latestFrameDesc.MipLevels,
        latestFrameDesc.ArraySize, latestFrameDesc.SampleDesc.Count);
    return {};
}

std::expected<void, std::error_code>
OnnxDmlCaptureProcessor::initializeD3d12AndDmlDeviceIfNeeded() {
    if (d3d12Device.get() != nullptr && d3d12Queue.get() != nullptr && dmlDevice.get() != nullptr) {
        return {};
    }

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    const std::expected<void, std::error_code> queryDxgiDeviceResult =
        checkD3dCall(d3d11Device->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void()),
                     "ID3D11Device::QueryInterface(IDXGIDevice)");
    if (!queryDxgiDeviceResult) {
        return std::unexpected(queryDxgiDeviceResult.error());
    }

    winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
    const std::expected<void, std::error_code> getAdapterResult =
        checkD3dCall(dxgiDevice->GetAdapter(dxgiAdapter.put()), "IDXGIDevice::GetAdapter");
    if (!getAdapterResult) {
        return std::unexpected(getAdapterResult.error());
    }

    const std::expected<void, std::error_code> createD3d12DeviceResult =
        checkD3dCall(D3D12CreateDevice(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0,
                                       __uuidof(ID3D12Device), d3d12Device.put_void()),
                     "D3D12CreateDevice");
    if (!createD3d12DeviceResult) {
        return std::unexpected(createD3d12DeviceResult.error());
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    const std::expected<void, std::error_code> createQueueResult =
        checkD3dCall(d3d12Device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                                     d3d12Queue.put_void()),
                     "ID3D12Device::CreateCommandQueue");
    if (!createQueueResult) {
        return std::unexpected(createQueueResult.error());
    }

    const std::expected<void, std::error_code> loadModuleResult =
        loadDirectmlModule(directmlModule);
    if (!loadModuleResult) {
        return std::unexpected(loadModuleResult.error());
    }

    const std::expected<void, std::error_code> createDmlDeviceResult =
        createDmlDevice(d3d12Device.get(), directmlModule, dmlDevice);
    if (!createDmlDeviceResult) {
        return std::unexpected(createDmlDeviceResult.error());
    }

    return {};
}

std::expected<void, std::error_code> OnnxDmlCaptureProcessor::startOnnxSession() {
    if (session == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    const std::expected<void, std::error_code> sessionStartResult =
        session->start(dmlDevice.get(), d3d12Queue.get());
    if (!sessionStartResult) {
        return std::unexpected(sessionStartResult.error());
    }
    return {};
}

std::expected<void, std::error_code> OnnxDmlCaptureProcessor::createSharedInputTextureResources() {
    D3D11_TEXTURE2D_DESC sharedInputDesc{};
    sharedInputDesc.Width = latestFrameDesc.Width;
    sharedInputDesc.Height = latestFrameDesc.Height;
    sharedInputDesc.MipLevels = latestFrameDesc.MipLevels;
    sharedInputDesc.ArraySize = latestFrameDesc.ArraySize;
    sharedInputDesc.Format = latestFrameDesc.Format;
    sharedInputDesc.SampleDesc = latestFrameDesc.SampleDesc;
    sharedInputDesc.Usage = D3D11_USAGE_DEFAULT;
    sharedInputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sharedInputDesc.CPUAccessFlags = 0;
    sharedInputDesc.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    const std::expected<void, std::error_code> createSharedTextureResult = checkD3dCall(
        d3d11Device->CreateTexture2D(&sharedInputDesc, nullptr, sharedInputTextureD3d11.put()),
        "ID3D11Device::CreateTexture2D(sharedInputTexture)");
    if (!createSharedTextureResult) {
        return std::unexpected(createSharedTextureResult.error());
    }

    winrt::com_ptr<IDXGIResource1> dxgiSharedResource;
    const std::expected<void, std::error_code> querySharedResourceResult =
        checkD3dCall(sharedInputTextureD3d11->QueryInterface(__uuidof(IDXGIResource1),
                                                             dxgiSharedResource.put_void()),
                     "ID3D11Texture2D::QueryInterface(IDXGIResource1)");
    if (!querySharedResourceResult) {
        return std::unexpected(querySharedResourceResult.error());
    }

    const std::expected<void, std::error_code> createSharedTextureHandleResult =
        checkD3dCall(dxgiSharedResource->CreateSharedHandle(
                         nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                         &preprocessSharedHandle),
                     "IDXGIResource1::CreateSharedHandle(inputTexture)");
    if (!createSharedTextureHandleResult) {
        return std::unexpected(createSharedTextureHandleResult.error());
    }

    const std::expected<void, std::error_code> openSharedTextureResult =
        checkD3dCall(d3d12Device->OpenSharedHandle(preprocessSharedHandle, __uuidof(ID3D12Resource),
                                                   sharedInputTextureD3d12.put_void()),
                     "ID3D12Device::OpenSharedHandle(inputTexture)");
    if (!openSharedTextureResult) {
        return std::unexpected(openSharedTextureResult.error());
    }

    const std::expected<void, std::error_code> createInteropFenceResult =
        checkD3dCall(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence),
                                              interopFenceD3d12.put_void()),
                     "ID3D12Device::CreateFence(sharedInterop)");
    if (!createInteropFenceResult) {
        return std::unexpected(createInteropFenceResult.error());
    }

    const std::expected<void, std::error_code> createInteropFenceHandleResult =
        checkD3dCall(d3d12Device->CreateSharedHandle(interopFenceD3d12.get(), nullptr, GENERIC_ALL,
                                                     nullptr, &interopFenceSharedHandle),
                     "ID3D12Device::CreateSharedHandle(sharedInteropFence)");
    if (!createInteropFenceHandleResult) {
        return std::unexpected(createInteropFenceHandleResult.error());
    }

    const std::expected<void, std::error_code> openInteropFenceResult =
        checkD3dCall(d3d11Device5->OpenSharedFence(interopFenceSharedHandle, __uuidof(ID3D11Fence),
                                                   interopFenceD3d11.put_void()),
                     "ID3D11Device5::OpenSharedFence(sharedInteropFence)");
    if (!openInteropFenceResult) {
        return std::unexpected(openInteropFenceResult.error());
    }
    interopFenceValue = 0;

    const std::expected<void, std::error_code> preprocessInitResult =
        initializePreprocessResources();
    if (!preprocessInitResult) {
        return std::unexpected(preprocessInitResult.error());
    }
    return {};
}

std::expected<void, std::error_code>
OnnxDmlCaptureProcessor::dispatchPreprocess(std::uint64_t inputReadyFenceValue) {
    if (!gpuReady || session == nullptr || d3d12Queue.get() == nullptr ||
        preprocessCommandAllocator.get() == nullptr || preprocessCommandList.get() == nullptr ||
        preprocessPipelineState.get() == nullptr || preprocessRootSignature.get() == nullptr ||
        preprocessDescriptorHeap.get() == nullptr || sharedInputTextureD3d12.get() == nullptr ||
        preprocessOutputResourceD3d12.get() == nullptr || preprocessFence.get() == nullptr ||
        preprocessFenceEvent == nullptr || interopFenceD3d12.get() == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    const std::expected<void, std::error_code> waitSharedInputResult =
        checkD3dCall(d3d12Queue->Wait(interopFenceD3d12.get(), inputReadyFenceValue),
                     "ID3D12CommandQueue::Wait(sharedInput)");
    if (!waitSharedInputResult) {
        return std::unexpected(waitSharedInputResult.error());
    }

    const OnnxDmlSession::ModelMetadata& modelMetadata = session->metadata();
    const std::array<std::uint32_t, 4> constants = {latestFrameDesc.Width, latestFrameDesc.Height,
                                                    modelMetadata.inputWidth,
                                                    modelMetadata.inputHeight};

    const std::expected<void, std::error_code> resetAllocatorResult =
        checkD3dCall(preprocessCommandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    if (!resetAllocatorResult) {
        return std::unexpected(resetAllocatorResult.error());
    }

    const std::expected<void, std::error_code> resetCommandListResult =
        checkD3dCall(preprocessCommandList->Reset(preprocessCommandAllocator.get(),
                                                  preprocessPipelineState.get()),
                     "ID3D12GraphicsCommandList::Reset");
    if (!resetCommandListResult) {
        return std::unexpected(resetCommandListResult.error());
    }

    ID3D12DescriptorHeap* descriptorHeaps[] = {preprocessDescriptorHeap.get()};
    preprocessCommandList->SetDescriptorHeaps(1, descriptorHeaps);
    preprocessCommandList->SetComputeRootSignature(preprocessRootSignature.get());
    preprocessCommandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(constants.size()),
                                                        constants.data(), 0);

    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle =
        preprocessDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle = srvGpuHandle;
    uavGpuHandle.ptr += preprocessDescriptorIncrement;
    preprocessCommandList->SetComputeRootDescriptorTable(1, srvGpuHandle);
    preprocessCommandList->SetComputeRootDescriptorTable(2, uavGpuHandle);

    D3D12_RESOURCE_BARRIER beforeDispatch[2]{};
    beforeDispatch[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    beforeDispatch[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    beforeDispatch[0].Transition.pResource = sharedInputTextureD3d12.get();
    beforeDispatch[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    beforeDispatch[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    beforeDispatch[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    beforeDispatch[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    beforeDispatch[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    beforeDispatch[1].Transition.pResource = preprocessOutputResourceD3d12.get();
    beforeDispatch[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    beforeDispatch[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    beforeDispatch[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    preprocessCommandList->ResourceBarrier(static_cast<UINT>(std::size(beforeDispatch)),
                                           beforeDispatch);

    const UINT dispatchX = (modelMetadata.inputWidth + 7U) / 8U;
    const UINT dispatchY = (modelMetadata.inputHeight + 7U) / 8U;
    preprocessCommandList->Dispatch(dispatchX, dispatchY, 1U);

    D3D12_RESOURCE_BARRIER afterDispatch[2]{};
    afterDispatch[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    afterDispatch[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    afterDispatch[0].Transition.pResource = sharedInputTextureD3d12.get();
    afterDispatch[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    afterDispatch[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    afterDispatch[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

    afterDispatch[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    afterDispatch[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    afterDispatch[1].Transition.pResource = preprocessOutputResourceD3d12.get();
    afterDispatch[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    afterDispatch[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    afterDispatch[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    preprocessCommandList->ResourceBarrier(static_cast<UINT>(std::size(afterDispatch)),
                                           afterDispatch);

    const std::expected<void, std::error_code> closeCommandListResult =
        checkD3dCall(preprocessCommandList->Close(), "ID3D12GraphicsCommandList::Close");
    if (!closeCommandListResult) {
        return std::unexpected(closeCommandListResult.error());
    }

    ID3D12CommandList* commandLists[] = {preprocessCommandList.get()};
    d3d12Queue->ExecuteCommandLists(static_cast<UINT>(std::size(commandLists)), commandLists);

    ++preprocessFenceValue;
    const std::expected<void, std::error_code> queueSignalResult =
        checkD3dCall(d3d12Queue->Signal(preprocessFence.get(), preprocessFenceValue),
                     "ID3D12CommandQueue::Signal(preprocess)");
    if (!queueSignalResult) {
        return std::unexpected(queueSignalResult.error());
    }

    if (preprocessFence->GetCompletedValue() < preprocessFenceValue) {
        const std::expected<void, std::error_code> setEventResult = checkD3dCall(
            preprocessFence->SetEventOnCompletion(preprocessFenceValue, preprocessFenceEvent),
            "ID3D12Fence::SetEventOnCompletion(preprocess)");
        if (!setEventResult) {
            return std::unexpected(setEventResult.error());
        }

        const DWORD waitResult = WaitForSingleObject(preprocessFenceEvent, INFINITE);
        const std::expected<void, std::error_code> waitEventResult = checkWin32Call(
            waitResult == WAIT_OBJECT_0, "WaitForSingleObject(preprocessFenceEvent)");
        if (!waitEventResult) {
            return std::unexpected(waitEventResult.error());
        }
    }

    return {};
}

void OnnxDmlCaptureProcessor::releaseGpuResources() {
    interopFenceD3d11 = nullptr;
    interopFenceD3d12 = nullptr;
    preprocessFence = nullptr;
    preprocessCommandList = nullptr;
    preprocessCommandAllocator = nullptr;
    preprocessDescriptorHeap = nullptr;
    preprocessPipelineState = nullptr;
    preprocessRootSignature = nullptr;
    preprocessOutputResourceD3d12 = nullptr;
    sharedInputTextureD3d12 = nullptr;
    sharedInputTextureD3d11 = nullptr;
    d3d11Context4 = nullptr;
    d3d11Context = nullptr;
    d3d11Device5 = nullptr;
    d3d11Device = nullptr;
    preprocessDescriptorIncrement = 0;
    preprocessFenceValue = 0;
    interopFenceValue = 0;

    if (preprocessFenceEvent != nullptr) {
        const BOOL closeEventResult = CloseHandle(preprocessFenceEvent);
        if (closeEventResult == FALSE) {
            VF_WARN("OnnxDmlCaptureProcessor failed to close preprocess fence event");
        }
        preprocessFenceEvent = nullptr;
    }

    if (preprocessSharedHandle != nullptr) {
        const BOOL closeResult = CloseHandle(preprocessSharedHandle);
        if (closeResult == FALSE) {
            VF_WARN("OnnxDmlCaptureProcessor failed to close shared handle");
        }
        preprocessSharedHandle = nullptr;
    }

    if (interopFenceSharedHandle != nullptr) {
        const BOOL closeResult = CloseHandle(interopFenceSharedHandle);
        if (closeResult == FALSE) {
            VF_WARN("OnnxDmlCaptureProcessor failed to close interop fence shared handle");
        }
        interopFenceSharedHandle = nullptr;
    }

    gpuReady = false;
}

void OnnxDmlCaptureProcessor::onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) {
    if (texture == nullptr) {
        return;
    }

    {
        std::scoped_lock lock(stateMutex);
        if (state != ProcessorState::Running || gpuFault) {
            return;
        }
    }

    std::uint64_t frameFenceValue = 0;

    try {
        std::scoped_lock gpuLock(gpuMutex);

        if (!gpuReady) {
            const std::expected<void, std::error_code> initResult = initializeGpuResources(texture);
            if (!initResult) {
                gpuFault = true;
                std::scoped_lock stateLock(stateMutex);
                state = ProcessorState::Fault;
                VF_ERROR("OnnxDmlCaptureProcessor GPU initialization failed: {}",
                         initResult.error().message());
                return;
            }
        }

        D3D11_TEXTURE2D_DESC incomingDesc{};
        texture->GetDesc(&incomingDesc);
        if (incomingDesc.Width != latestFrameDesc.Width ||
            incomingDesc.Height != latestFrameDesc.Height ||
            incomingDesc.Format != latestFrameDesc.Format) {
            VF_WARN("OnnxDmlCaptureProcessor frame format changed. Reinitializing GPU resources.");
            releaseGpuResources();
            const std::expected<void, std::error_code> reinitResult =
                initializeGpuResources(texture);
            if (!reinitResult) {
                gpuFault = true;
                std::scoped_lock stateLock(stateMutex);
                state = ProcessorState::Fault;
                VF_ERROR("OnnxDmlCaptureProcessor GPU reinitialization failed: {}",
                         reinitResult.error().message());
                return;
            }
        }

        if (sharedInputTextureD3d11.get() == nullptr || d3d11Context.get() == nullptr ||
            d3d11Context4.get() == nullptr || interopFenceD3d11.get() == nullptr) {
            gpuFault = true;
            std::scoped_lock stateLock(stateMutex);
            state = ProcessorState::Fault;
            VF_ERROR("OnnxDmlCaptureProcessor frame copy skipped due to uninitialized resources");
            return;
        }

        d3d11Context->CopyResource(sharedInputTextureD3d11.get(), texture);
        ++interopFenceValue;
        frameFenceValue = interopFenceValue;

        const std::expected<void, std::error_code> signalSharedFenceResult =
            checkD3dCall(d3d11Context4->Signal(interopFenceD3d11.get(), frameFenceValue),
                         "ID3D11DeviceContext4::Signal(sharedInput)");
        if (!signalSharedFenceResult) {
            gpuFault = true;
            std::scoped_lock stateLock(stateMutex);
            state = ProcessorState::Fault;
            VF_ERROR("OnnxDmlCaptureProcessor failed to signal D3D11->D3D12 interop fence");
            return;
        }
    } catch (const winrt::hresult_error& ex) {
        std::scoped_lock stateLock(stateMutex);
        state = ProcessorState::Fault;
        gpuFault = true;
        VF_ERROR("OnnxDmlCaptureProcessor failed to capture frame: {} (HRESULT=0x{:08X})",
                 winrt::to_string(ex.message()), static_cast<std::uint32_t>(ex.code().value));
        return;
    }

    {
        std::scoped_lock lock(frameMutex);
        if (hasPendingFrame) {
            ++droppedFrames;
        }
        hasPendingFrame = true;
        pendingTimestamp100ns = info.systemRelativeTime100ns;
        pendingFrameFenceValue = frameFenceValue;
    }
    frameCv.notify_one();
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access)
#else

void OnnxDmlCaptureProcessor::onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) {
    static_cast<void>(texture);
    static_cast<void>(info);
}
#endif

void OnnxDmlCaptureProcessor::inferenceLoop(const std::stop_token& stopToken) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    while (!stopToken.stop_requested()) {
        std::int64_t timestamp100ns = 0;
        std::uint64_t frameFenceValue = 0;

        {
            std::unique_lock lock(frameMutex);
            frameCv.wait(lock, [this, &stopToken]() {
                return stopToken.stop_requested() || hasPendingFrame;
            });
            if (stopToken.stop_requested()) {
                break;
            }

            timestamp100ns = pendingTimestamp100ns;
            frameFenceValue = pendingFrameFenceValue;
            hasPendingFrame = false;

            const auto now = std::chrono::steady_clock::now();
            if (droppedFrames > 0 && now - lastDropLogTime >= std::chrono::seconds(1)) {
                VF_DEBUG("OnnxDmlCaptureProcessor dropped {} stale frame(s)", droppedFrames);
                droppedFrames = 0;
                lastDropLogTime = now;
            }
        }

        std::expected<InferenceResult, std::error_code> inferenceResult =
            std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
        {
            std::scoped_lock gpuLock(gpuMutex);

            const std::expected<void, std::error_code> preprocessResult =
                dispatchPreprocess(frameFenceValue);
            if (!preprocessResult) {
                std::scoped_lock stateLock(stateMutex);
                state = ProcessorState::Fault;
                gpuFault = true;
                VF_ERROR("OnnxDmlCaptureProcessor preprocess failed: {}",
                         preprocessResult.error().message());
                return;
            }

            if (session == nullptr) {
                std::scoped_lock stateLock(stateMutex);
                state = ProcessorState::Fault;
                gpuFault = true;
                VF_ERROR("OnnxDmlCaptureProcessor session is null during inference");
                return;
            }
            inferenceResult = session->runWithGpuInput(timestamp100ns);
        }

        if (!inferenceResult) {
            VF_WARN("OnnxDmlCaptureProcessor inference failed: {}",
                    inferenceResult.error().message());
            continue;
        }

        resultStore.publish(inferenceResult.value());
    }
#else
    static_cast<void>(stopToken);
#endif
}

} // namespace vf
