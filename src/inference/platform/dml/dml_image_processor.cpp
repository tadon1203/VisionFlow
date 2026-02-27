#include "inference/platform/dml/dml_image_processor.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <mutex>
#include <system_error>

#include "VisionFlow/inference/inference_error.hpp"
#include "inference/platform/dml/dml_image_processor_interop.hpp"
#include "inference/platform/dml/dml_image_processor_preprocess.hpp"
#include "inference/platform/dml/dx_utils.hpp"
#include "inference/platform/dml/onnx_dml_session.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#endif

namespace vf {

#ifdef _WIN32
class DmlImageProcessor::Impl {
  public:
    Impl(OnnxDmlSession& session, std::shared_ptr<IProfiler> profiler)
        : session(session), profiler(std::move(profiler)) {}

    std::expected<InitializeResult, std::error_code> initialize(ID3D11Texture2D* sourceTexture) {
        if (sourceTexture == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }

        std::scoped_lock lock(mutex);

        const auto interopResult = interop.initializeOrUpdate(sourceTexture);
        if (!interopResult) {
            return std::unexpected(interopResult.error());
        }
        const DmlInteropUpdateResult& interopState = interopResult.value();

        const auto setupResult = prepareInferencePath(interopState);
        if (!setupResult) {
            return std::unexpected(setupResult.error());
        }

        initialized = true;
        return InitializeResult{
            .dmlDevice = interopState.dmlDevice,
            .commandQueue = interopState.commandQueue,
        };
    }

    std::expected<DispatchResult, std::error_code> dispatch(ID3D11Texture2D* frameTexture,
                                                            std::uint64_t fenceValue) {
        if (frameTexture == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::RunFailed));
        }

        std::scoped_lock lock(mutex);
        if (!initialized) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }

        const auto interopInitResult = interop.initializeOrUpdate(frameTexture);
        if (!interopInitResult) {
            return std::unexpected(interopInitResult.error());
        }
        const DmlInteropUpdateResult& interopState = interopInitResult.value();

        const auto setupResult = prepareInferencePath(interopState);
        if (!setupResult) {
            return std::unexpected(setupResult.error());
        }

        const auto copyResult = interop.copyAndSignal(frameTexture, fenceValue);
        if (!copyResult) {
            return std::unexpected(copyResult.error());
        }

        const auto waitResult = interop.waitOnQueue(copyResult.value());
        if (!waitResult) {
            return std::unexpected(waitResult.error());
        }

        const auto prepareResult = preprocess.prepareForRecord();
        if (!prepareResult) {
            return std::unexpected(prepareResult.error());
        }

        const auto recordResult =
            preprocess.recordDispatch(interopState.sourceWidth, interopState.sourceHeight);
        if (!recordResult) {
            return std::unexpected(recordResult.error());
        }

        const auto closeResult = preprocess.closeAfterRecord();
        if (!closeResult) {
            return std::unexpected(closeResult.error());
        }

        ID3D12CommandQueue* queue = interopState.commandQueue;
        ID3D12CommandList* commandList = preprocess.commandListForExecute();
        ID3D12Fence* completionFence = preprocess.getCompletionFence();
        HANDLE completionEvent = preprocess.getCompletionEvent();

        if (queue == nullptr || commandList == nullptr || completionFence == nullptr ||
            completionEvent == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }

        std::array<ID3D12CommandList*, 1> commandLists = {commandList};
        queue->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());

        const std::uint64_t completionFenceValue = preprocess.nextFenceValue();
        const auto signalResult =
            dx_utils::callD3d(queue->Signal(completionFence, completionFenceValue),
                              "ID3D12CommandQueue::Signal(preprocess)", InferenceError::RunFailed);
        if (!signalResult) {
            return std::unexpected(signalResult.error());
        }

        if (completionFence->GetCompletedValue() < completionFenceValue) {
            const auto setEventResult = dx_utils::callD3d(
                completionFence->SetEventOnCompletion(completionFenceValue, completionEvent),
                "ID3D12Fence::SetEventOnCompletion(preprocess)", InferenceError::RunFailed);
            if (!setEventResult) {
                return std::unexpected(setEventResult.error());
            }

            const DWORD waitCode = WaitForSingleObject(completionEvent, INFINITE);
            const auto waitEventResult = dx_utils::callWin32(
                waitCode == WAIT_OBJECT_0, "WaitForSingleObject(preprocessFenceEvent)",
                InferenceError::RunFailed);
            if (!waitEventResult) {
                return std::unexpected(waitEventResult.error());
            }
        }

        if (profiler != nullptr) {
            UINT64 timestampFrequency = 0;
            const HRESULT frequencyResult = queue->GetTimestampFrequency(&timestampFrequency);
            const auto frequencyCheck =
                dx_utils::checkD3d(frequencyResult, "ID3D12CommandQueue::GetTimestampFrequency");
            if (!frequencyCheck || timestampFrequency == 0) {
                VF_WARN("Failed to get D3D12 timestamp frequency for profiling");
            } else {
                const auto gpuDurationUs = preprocess.readLastGpuDurationUs(timestampFrequency);
                if (!gpuDurationUs) {
                    VF_WARN("Failed to read preprocess GPU timestamp: {}",
                            gpuDurationUs.error().message());
                } else {
                    profiler->recordGpuUs(ProfileStage::GpuPreprocess, gpuDurationUs.value());
                }
            }
        }

        return DispatchResult{
            .outputResource = preprocess.getOutputResource(),
            .outputBytes = preprocess.getOutputBytes(),
        };
    }

    void shutdown() {
        std::scoped_lock lock(mutex);
        preprocess.reset();
        interop.reset();
        initialized = false;
    }

  private:
    std::expected<void, std::error_code> prepareInferencePath(const DmlInteropUpdateResult& state) {
        const auto sessionStartResult =
            session.start(state.dmlDevice, state.commandQueue, state.generationId);
        if (!sessionStartResult) {
            return std::unexpected(sessionStartResult.error());
        }

        const OnnxDmlSession::ModelMetadata& metadata = session.metadata();
        DmlImageProcessorPreprocess::InitConfig initConfig{};
        initConfig.dstWidth = metadata.inputWidth;
        initConfig.dstHeight = metadata.inputHeight;
        initConfig.outputBytes = metadata.inputTensorBytes;
        initConfig.outputElementCount = metadata.inputElementCount;

        const auto pipelineInitResult = preprocess.initialize(state.device, initConfig);
        if (!pipelineInitResult) {
            return std::unexpected(pipelineInitResult.error());
        }

        const auto pipelineUpdateResult = preprocess.updateResources(
            state.sharedInputTexture, static_cast<DXGI_FORMAT>(state.sourceFormat));
        if (!pipelineUpdateResult) {
            return std::unexpected(pipelineUpdateResult.error());
        }

        return {};
    }

    OnnxDmlSession& session;
    std::shared_ptr<IProfiler> profiler;
    std::mutex mutex;
    bool initialized = false;
    DmlImageProcessorInterop interop;
    DmlImageProcessorPreprocess preprocess;
};

DmlImageProcessor::DmlImageProcessor(OnnxDmlSession& session, std::shared_ptr<IProfiler> profiler)
    : impl(std::make_unique<Impl>(session, std::move(profiler))) {}

DmlImageProcessor::~DmlImageProcessor() { impl->shutdown(); }

std::expected<DmlImageProcessor::InitializeResult, std::error_code>
DmlImageProcessor::initialize(ID3D11Texture2D* sourceTexture) {
    return impl->initialize(sourceTexture);
}

std::expected<DmlImageProcessor::DispatchResult, std::error_code>
DmlImageProcessor::dispatch(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue) {
    return impl->dispatch(frameTexture, fenceValue);
}

void DmlImageProcessor::shutdown() { impl->shutdown(); }

#else

class DmlImageProcessor::Impl {
  public:
    Impl(OnnxDmlSession& session, std::shared_ptr<IProfiler> profiler) {
        static_cast<void>(session);
        static_cast<void>(profiler);
    }
};

DmlImageProcessor::DmlImageProcessor(OnnxDmlSession& session, std::shared_ptr<IProfiler> profiler)
    : impl(std::make_unique<Impl>(session, std::move(profiler))) {}

DmlImageProcessor::~DmlImageProcessor() = default;

std::expected<DmlImageProcessor::InitializeResult, std::error_code>
DmlImageProcessor::initialize(void* sourceTexture) {
    static_cast<void>(sourceTexture);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<DmlImageProcessor::DispatchResult, std::error_code>
DmlImageProcessor::dispatch(void* frameTexture, std::uint64_t fenceValue) {
    static_cast<void>(frameTexture);
    static_cast<void>(fenceValue);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

void DmlImageProcessor::shutdown() {}

#endif

} // namespace vf
