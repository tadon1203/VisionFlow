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
#include "inference/platform/dml/onnx_dml_session.hpp"

#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
#include <d3d12.h>
#include <dxgi.h>

namespace vf {

namespace {

[[nodiscard]] bool isPositiveOrDynamicDimension(int64_t dim) noexcept {
    return dim > 0 || dim == -1;
}

[[nodiscard]] std::expected<void, std::error_code>
validateNchwImageInputShape(const std::vector<int64_t>& inputShape) {
    if (inputShape.size() != 4U) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    for (const int64_t dim : inputShape) {
        if (!isPositiveOrDynamicDimension(dim)) {
            return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
        }
    }

    const int64_t batch = inputShape.at(0);
    const int64_t channels = inputShape.at(1);
    const int64_t height = inputShape.at(2);
    const int64_t width = inputShape.at(3);

    if (batch != 1 && batch != -1) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (channels != 3 && channels != -1) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (!isPositiveOrDynamicDimension(height) || !isPositiveOrDynamicDimension(width)) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (batch == -1 || channels == -1 || height == -1 || width == -1) {
        VF_ERROR("Dynamic input shape is not supported for DirectML preprocessing");
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    return {};
}

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
    const auto shapeValidationResult = validateNchwImageInputShape(inputShape);
    if (!shapeValidationResult) {
        return std::unexpected(shapeValidationResult.error());
    }

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

[[nodiscard]] bool isDeviceLostHresult(HRESULT hr) noexcept {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG;
}

} // namespace

std::expected<void, std::error_code> OnnxDmlSession::start(IDMLDevice* dmlDevice,
                                                           ID3D12CommandQueue* commandQueue,
                                                           std::uint64_t interopGeneration) {
    if (running) {
        const bool sameDevice = this->dmlDevice.get() == dmlDevice;
        const bool sameQueue = d3d12Queue.get() == commandQueue;
        const bool sameGeneration = boundInteropGeneration == interopGeneration;
        if (sameDevice && sameQueue && sameGeneration) {
            return {};
        }

        const auto stopResult = stop();
        if (!stopResult) {
            return std::unexpected(stopResult.error());
        }
    }
    if (dmlDevice == nullptr || commandQueue == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }

    try {
        const std::filesystem::path resolvedModelPath = resolveModelPath();
        if (!std::filesystem::exists(resolvedModelPath)) {
            VF_ERROR("Inference model was not found: {}", resolvedModelPath.string());
            return std::unexpected(makeErrorCode(CaptureError::InferenceModelNotFound));
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

        boundInteropGeneration = interopGeneration;
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
        const auto error = isDeviceLostHresult(ex.code().value)
                               ? CaptureError::InferenceDeviceLost
                               : CaptureError::InferenceInitializationFailed;
        return std::unexpected(makeErrorCode(error));
    } catch (const std::exception& ex) {
        VF_ERROR("OnnxDmlSession start failed with exception: {}", ex.what());
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    } catch (...) {
        VF_ERROR("OnnxDmlSession start failed with unknown exception");
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }
}

std::expected<void, std::error_code> OnnxDmlSession::start(IDMLDevice* dmlDevice,
                                                           ID3D12CommandQueue* commandQueue) {
    return start(dmlDevice, commandQueue, 0);
}

std::expected<void, std::error_code> OnnxDmlSession::stop() {
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
    boundInputResource = nullptr;
    boundInteropGeneration = 0;
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
}

std::expected<InferenceResult, std::error_code>
OnnxDmlSession::runWithGpuInput(std::int64_t frameTimestamp100ns, ID3D12Resource* resource,
                                std::size_t resourceBytes) {
    if (!running || session == nullptr || d3d12Device == nullptr || dmlApi == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    }
    if (resource == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    }

    if (resourceBytes < modelMetadata.inputTensorBytes) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
    }

    if (inputAllocation != nullptr && boundInputResource != resource) {
        OrtStatus* status = dmlApi->FreeGPUAllocation(inputAllocation);
        if (status != nullptr) {
            Ort::GetApi().ReleaseStatus(status);
        }
        inputAllocation = nullptr;
        boundInputResource = nullptr;
    }

    try {
        if (inputAllocation == nullptr) {
            Ort::ThrowOnError(
                dmlApi->CreateGPUAllocationFromD3DResource(resource, &inputAllocation));
            boundInputResource = resource;
            VF_DEBUG("OnnxDmlSession bound D3D12 input resource ({} bytes)",
                     modelMetadata.inputTensorBytes);
        }

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
            Ort::Value& outputValue = outputValues.at(i);
            if (!outputValue.IsTensor()) {
                return std::unexpected(makeErrorCode(CaptureError::InferenceRunFailed));
            }

            Ort::TensorTypeAndShapeInfo tensorInfo = outputValue.GetTensorTypeAndShapeInfo();
            if (tensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
            }

            InferenceTensor tensor;
            tensor.name = modelMetadata.outputNames.at(i);
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

} // namespace vf

#endif
