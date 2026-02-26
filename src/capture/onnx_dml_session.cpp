#include "capture/onnx_dml_session.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
#include <d3d12.h>
#endif

namespace vf {

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
namespace {

std::expected<OnnxDmlSession::ModelMetadata, std::error_code>
readModelMetadata(Ort::Session& session) {
    const std::size_t inputCount = session.GetInputCount();
    if (inputCount != 1) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    Ort::AllocatorWithDefaultOptions allocator;
    std::string inputName;
    {
        auto inputNameAllocated = session.GetInputNameAllocated(0, allocator);
        inputName = inputNameAllocated.get();
    }

    const Ort::TypeInfo inputTypeInfo = session.GetInputTypeInfo(0);
    const Ort::ConstTensorTypeAndShapeInfo inputTensorInfo =
        inputTypeInfo.GetTensorTypeAndShapeInfo();
    if (inputTensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    std::vector<int64_t> inputShape = inputTensorInfo.GetShape();
    const std::size_t outputCount = session.GetOutputCount();
    if (outputCount == 0U) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    std::vector<std::string> outputNames;
    outputNames.reserve(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) {
        auto outputName = session.GetOutputNameAllocated(i, allocator);
        outputNames.emplace_back(outputName.get());
    }

    return OnnxDmlSession::createModelMetadata(std::move(inputName), std::move(inputShape),
                                               std::move(outputNames));
}

} // namespace
#endif

OnnxDmlSession::OnnxDmlSession(std::filesystem::path modelPath) : modelPath(std::move(modelPath)) {}

std::expected<OnnxDmlSession::ModelMetadata, std::error_code>
OnnxDmlSession::createModelMetadata(std::string inputName, std::vector<int64_t> inputShape,
                                    std::vector<std::string> outputNames) {
    if (inputShape.size() != 4) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    for (const int64_t dim : inputShape) {
        if (dim <= 0) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
        }
    }

    if (inputShape[0] != 1) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (inputShape[1] != 3) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (outputNames.empty()) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    ModelMetadata metadata;
    metadata.inputName = std::move(inputName);
    metadata.outputNames = std::move(outputNames);
    metadata.inputShape = std::move(inputShape);
    metadata.inputChannels = static_cast<std::uint32_t>(metadata.inputShape[1]);
    metadata.inputHeight = static_cast<std::uint32_t>(metadata.inputShape[2]);
    metadata.inputWidth = static_cast<std::uint32_t>(metadata.inputShape[3]);

    metadata.inputElementCount = static_cast<std::size_t>(metadata.inputShape[0]) *
                                 static_cast<std::size_t>(metadata.inputShape[1]) *
                                 static_cast<std::size_t>(metadata.inputShape[2]) *
                                 static_cast<std::size_t>(metadata.inputShape[3]);
    metadata.inputTensorBytes = metadata.inputElementCount * sizeof(float);

    return metadata;
}

OnnxDmlSession::~OnnxDmlSession() {
    const std::expected<void, std::error_code> result = stop();
    if (!result) {
        VF_WARN("OnnxDmlSession stop during destruction failed: {}", result.error().message());
    }
}

std::filesystem::path OnnxDmlSession::resolveModelPath() const {
    if (modelPath.is_absolute()) {
        return modelPath;
    }
    return std::filesystem::current_path() / "models" / modelPath;
}

const OnnxDmlSession::ModelMetadata& OnnxDmlSession::metadata() const { return modelMetadata; }

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
std::expected<void, std::error_code> OnnxDmlSession::start(IDMLDevice* dmlDevice,
                                                           ID3D12CommandQueue* commandQueue) {
    if (running) {
        return {};
    }
    if (dmlDevice == nullptr || commandQueue == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    try {
        const std::filesystem::path resolvedModelPath = resolveModelPath();
        if (!std::filesystem::exists(resolvedModelPath)) {
            VF_ERROR("Inference model was not found: {}", resolvedModelPath.string());
            return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
        }

        ortEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "VisionFlow");
        sessionOptions = std::make_unique<Ort::SessionOptions>();
        sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        sessionOptions->DisableMemPattern();
        sessionOptions->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        const OrtApi& ortApi = Ort::GetApi();
        const void* dmlApiRaw = nullptr;
        Ort::ThrowOnError(ortApi.GetExecutionProviderApi("DML", ORT_API_VERSION, &dmlApiRaw));
        dmlApi = reinterpret_cast<const OrtDmlApi*>(dmlApiRaw);

        this->dmlDevice.copy_from(dmlDevice);
        d3d12Queue.copy_from(commandQueue);
        winrt::check_hresult(d3d12Queue->GetDevice(__uuidof(ID3D12Device), d3d12Device.put_void()));

        Ort::ThrowOnError(dmlApi->SessionOptionsAppendExecutionProvider_DML1(
            *sessionOptions, this->dmlDevice.get(), d3d12Queue.get()));

        const std::wstring modelPathWide = resolvedModelPath.wstring();
        session = std::make_unique<Ort::Session>(*ortEnv, modelPathWide.c_str(), *sessionOptions);

        const auto metadataResult = readModelMetadata(*session);
        if (!metadataResult) {
            return std::unexpected(metadataResult.error());
        }
        modelMetadata = metadataResult.value();

        dmlMemoryInfo = std::make_unique<Ort::MemoryInfo>(
            "DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);
        cpuMemoryInfo = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        running = true;

        VF_INFO("OnnxDmlSession started");
        VF_DEBUG(
            "OnnxDmlSession details: model='{}', input='{}', shape=[1, {}, {}, {}], outputs={}",
            resolvedModelPath.string(), modelMetadata.inputName, modelMetadata.inputChannels,
            modelMetadata.inputHeight, modelMetadata.inputWidth, modelMetadata.outputNames.size());
        return {};
    } catch (const Ort::Exception& ex) {
        VF_ERROR("OnnxDmlSession start failed with ORT exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    } catch (const winrt::hresult_error& ex) {
        VF_ERROR("OnnxDmlSession start failed with WinRT exception: {} (HRESULT=0x{:08X})",
                 winrt::to_string(ex.message()), static_cast<std::uint32_t>(ex.code().value));
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    } catch (const std::exception& ex) {
        VF_ERROR("OnnxDmlSession start failed with exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    } catch (...) {
        VF_ERROR("OnnxDmlSession start failed with unknown exception");
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }
}
#elif defined(_WIN32)
std::expected<void, std::error_code> OnnxDmlSession::start(IDMLDevice* dmlDevice,
                                                           ID3D12CommandQueue* commandQueue) {
    static_cast<void>(dmlDevice);
    static_cast<void>(commandQueue);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}
#else
std::expected<void, std::error_code> OnnxDmlSession::start(void* dmlDevice, void* commandQueue) {
    static_cast<void>(dmlDevice);
    static_cast<void>(commandQueue);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
}
#endif

std::expected<void, std::error_code> OnnxDmlSession::stop() {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    if (!running) {
        return {};
    }

    if (dmlApi != nullptr && inputAllocation != nullptr) {
        OrtStatus* status = dmlApi->FreeGPUAllocation(inputAllocation);
        if (status != nullptr) {
            Ort::GetApi().ReleaseStatus(status);
        }
    }

    inputAllocation = nullptr;
    inputBound = false;
    inputResource = nullptr;
    dmlDevice = nullptr;
    d3d12Device = nullptr;
    d3d12Queue = nullptr;

    session.reset();
    sessionOptions.reset();
    ortEnv.reset();
    dmlMemoryInfo.reset();
    cpuMemoryInfo.reset();
    dmlApi = nullptr;

    running = false;
    return {};
#else
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#endif
}

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML

std::expected<void, std::error_code>
OnnxDmlSession::initializeGpuInputFromResource(ID3D12Resource* resource,
                                               std::size_t resourceBytes) {
    if (!running || session == nullptr || d3d12Device == nullptr || dmlApi == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }
    if (resource == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceGpuInteropFailed));
    }

    if (resourceBytes < modelMetadata.inputTensorBytes) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceGpuInteropFailed));
    }

    if (inputAllocation != nullptr) {
        OrtStatus* status = dmlApi->FreeGPUAllocation(inputAllocation);
        if (status != nullptr) {
            Ort::GetApi().ReleaseStatus(status);
        }
        inputAllocation = nullptr;
        inputBound = false;
    }

    try {
        inputResource.copy_from(resource);
        Ort::ThrowOnError(
            dmlApi->CreateGPUAllocationFromD3DResource(inputResource.get(), &inputAllocation));
        inputBound = true;
        VF_INFO("OnnxDmlSession input is ready");
        VF_DEBUG("OnnxDmlSession bound shared D3D12 input resource ({} bytes)",
                 modelMetadata.inputTensorBytes);
        return {};
    } catch (const Ort::Exception& ex) {
        VF_ERROR("OnnxDmlSession input binding failed with ORT exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceGpuInteropFailed));
    } catch (const winrt::hresult_error& ex) {
        VF_ERROR("OnnxDmlSession input binding failed with WinRT exception: {} (HRESULT=0x{:08X})",
                 winrt::to_string(ex.message()), static_cast<std::uint32_t>(ex.code().value));
        return std::unexpected(makeErrorCode(CaptureError::InferenceGpuInteropFailed));
    } catch (const std::exception& ex) {
        VF_ERROR("OnnxDmlSession input binding failed with exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceGpuInteropFailed));
    } catch (...) {
        VF_ERROR("OnnxDmlSession input binding failed with unknown exception");
        return std::unexpected(makeErrorCode(CaptureError::InferenceGpuInteropFailed));
    }
}

std::expected<InferenceResult, std::error_code>
OnnxDmlSession::runWithGpuInput(std::int64_t frameTimestamp100ns) {
    if (!running || session == nullptr || !inputBound || inputAllocation == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    }

    try {
        Ort::Value inputTensor = Ort::Value::CreateTensor(
            *dmlMemoryInfo, inputAllocation, modelMetadata.inputTensorBytes,
            modelMetadata.inputShape.data(), modelMetadata.inputShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::IoBinding binding(*session);
        binding.BindInput(modelMetadata.inputName.c_str(), inputTensor);
        for (const std::string& outputName : modelMetadata.outputNames) {
            binding.BindOutput(outputName.c_str(), *cpuMemoryInfo);
        }

        Ort::RunOptions runOptions;
        binding.SynchronizeInputs();
        session->Run(runOptions, binding);
        binding.SynchronizeOutputs();

        std::vector<Ort::Value> outputValues = binding.GetOutputValues();
        if (outputValues.size() != modelMetadata.outputNames.size()) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
        }

        InferenceResult result;
        result.frameTimestamp100ns = frameTimestamp100ns;
        result.tensors.reserve(outputValues.size());

        for (std::size_t i = 0; i < outputValues.size(); ++i) {
            Ort::Value& outputValue = outputValues[i];
            if (!outputValue.IsTensor()) {
                return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
            }

            Ort::TensorTypeAndShapeInfo tensorInfo = outputValue.GetTensorTypeAndShapeInfo();
            if (tensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
            }

            InferenceTensor tensor;
            tensor.name = modelMetadata.outputNames[i];
            tensor.shape = tensorInfo.GetShape();

            const std::size_t elementCount = tensorInfo.GetElementCount();
            const auto* outputData = outputValue.GetTensorData<float>();
            tensor.values.assign(outputData, outputData + elementCount);
            result.tensors.emplace_back(std::move(tensor));
        }

        return result;
    } catch (const Ort::Exception& ex) {
        VF_WARN("OnnxDmlSession run failed with ORT exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    } catch (const std::exception& ex) {
        VF_WARN("OnnxDmlSession run failed with exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    } catch (...) {
        VF_WARN("OnnxDmlSession run failed with unknown exception");
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    }
}

#endif

} // namespace vf
