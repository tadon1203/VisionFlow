#include "inference/backend/dml/dml_image_processor.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <mutex>
#include <system_error>

#include "VisionFlow/inference/inference_error.hpp"
#include "inference/backend/dml/dml_image_processor_interop.hpp"
#include "inference/backend/dml/dml_image_processor_preprocess.hpp"
#include "inference/backend/dml/dx_utils.hpp"
#include "inference/backend/dml/onnx_dml_session.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#endif

namespace vf {

#ifdef _WIN32
class DmlImageProcessor::Impl {
  public:
    Impl(OnnxDmlSession& session, IProfiler* profiler) : session(session), profiler(profiler) {}

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

    std::expected<EnqueueStatus, std::error_code> enqueuePreprocess(ID3D11Texture2D* frameTexture,
                                                                    std::uint64_t fenceValue) {
        if (frameTexture == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::RunFailed));
        }

        std::scoped_lock lock(mutex);
        if (!initialized) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }
        if (preprocessSubmitted) {
            return EnqueueStatus::SkippedBusy;
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

        preprocessSubmitted = true;
        preprocessFenceValue = completionFenceValue;
        preprocessQueue = queue;
        preprocessCompletionFence = completionFence;
        preprocessCompletionEvent = completionEvent;

        return EnqueueStatus::Submitted;
    }

    std::expected<std::optional<DispatchResult>, std::error_code> tryCollectPreprocessResult() {
        std::scoped_lock lock(mutex);
        if (!initialized) {
            return std::unexpected(makeErrorCode(InferenceError::InitializationFailed));
        }
        if (!preprocessSubmitted) {
            return std::optional<DispatchResult>{};
        }
        if (preprocessCompletionFence == nullptr) {
            return std::unexpected(makeErrorCode(InferenceError::InvalidState));
        }
        if (preprocessCompletionFence->GetCompletedValue() < preprocessFenceValue) {
            return std::optional<DispatchResult>{};
        }

        if (profiler != nullptr && preprocessQueue != nullptr) {
            UINT64 timestampFrequency = 0;
            const HRESULT frequencyResult =
                preprocessQueue->GetTimestampFrequency(&timestampFrequency);
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

        preprocessSubmitted = false;
        return DispatchResult{
            .outputResource = preprocess.getOutputResource(),
            .outputBytes = preprocess.getOutputBytes(),
        };
    }

    void shutdown() {
        std::scoped_lock lock(mutex);
        if (preprocessSubmitted && preprocessCompletionFence != nullptr &&
            preprocessCompletionEvent != nullptr) {
            if (preprocessCompletionFence->GetCompletedValue() < preprocessFenceValue) {
                const auto setEventResult =
                    dx_utils::callD3d(preprocessCompletionFence->SetEventOnCompletion(
                                          preprocessFenceValue, preprocessCompletionEvent),
                                      "ID3D12Fence::SetEventOnCompletion(preprocessShutdown)",
                                      InferenceError::RunFailed);
                if (!setEventResult) {
                    VF_WARN("Shutdown wait setup failed: {}", setEventResult.error().message());
                } else {
                    const DWORD waitCode = WaitForSingleObject(preprocessCompletionEvent, INFINITE);
                    const auto waitEventResult = dx_utils::callWin32(
                        waitCode == WAIT_OBJECT_0, "WaitForSingleObject(preprocessShutdownEvent)",
                        InferenceError::RunFailed);
                    if (!waitEventResult) {
                        VF_WARN("Shutdown wait failed: {}", waitEventResult.error().message());
                    }
                }
            }
            preprocessSubmitted = false;
        }
        preprocess.reset();
        interop.reset();
        initialized = false;
        preprocessFenceValue = 0;
        preprocessQueue = nullptr;
        preprocessCompletionFence = nullptr;
        preprocessCompletionEvent = nullptr;
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
    IProfiler* profiler = nullptr;
    std::mutex mutex;
    bool initialized = false;
    bool preprocessSubmitted = false;
    std::uint64_t preprocessFenceValue = 0;
    ID3D12CommandQueue* preprocessQueue = nullptr;
    ID3D12Fence* preprocessCompletionFence = nullptr;
    HANDLE preprocessCompletionEvent = nullptr;
    DmlImageProcessorInterop interop;
    DmlImageProcessorPreprocess preprocess;
};

DmlImageProcessor::DmlImageProcessor(OnnxDmlSession& session, IProfiler* profiler)
    : impl(std::make_unique<Impl>(session, profiler)) {}

DmlImageProcessor::~DmlImageProcessor() noexcept {
    try {
        impl->shutdown();
    } catch (...) {
        VF_ERROR("DmlImageProcessor shutdown during destruction failed with exception");
    }
}

std::expected<DmlImageProcessor::InitializeResult, std::error_code>
DmlImageProcessor::initialize(ID3D11Texture2D* sourceTexture) {
    return impl->initialize(sourceTexture);
}

std::expected<DmlImageProcessor::EnqueueStatus, std::error_code>
DmlImageProcessor::enqueuePreprocess(ID3D11Texture2D* frameTexture, std::uint64_t fenceValue) {
    return impl->enqueuePreprocess(frameTexture, fenceValue);
}

std::expected<std::optional<DmlImageProcessor::DispatchResult>, std::error_code>
DmlImageProcessor::tryCollectPreprocessResult() {
    return impl->tryCollectPreprocessResult();
}

void DmlImageProcessor::shutdown() { impl->shutdown(); }

#else

class DmlImageProcessor::Impl {
  public:
    Impl(OnnxDmlSession& session, IProfiler* profiler) {
        static_cast<void>(session);
        static_cast<void>(profiler);
    }
};

DmlImageProcessor::DmlImageProcessor(OnnxDmlSession& session, IProfiler* profiler)
    : impl(std::make_unique<Impl>(session, profiler)) {}

DmlImageProcessor::~DmlImageProcessor() noexcept = default;

std::expected<DmlImageProcessor::InitializeResult, std::error_code>
DmlImageProcessor::initialize(void* sourceTexture) {
    static_cast<void>(sourceTexture);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<DmlImageProcessor::EnqueueStatus, std::error_code>
DmlImageProcessor::enqueuePreprocess(void* frameTexture, std::uint64_t fenceValue) {
    static_cast<void>(frameTexture);
    static_cast<void>(fenceValue);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<std::optional<DmlImageProcessor::DispatchResult>, std::error_code>
DmlImageProcessor::tryCollectPreprocessResult() {
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

void DmlImageProcessor::shutdown() {}

#endif

} // namespace vf
