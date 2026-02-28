#include "inference/engine/onnx_dml_inference_processor.hpp"

#include <expected>
#include <memory>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/core/logger.hpp"
#include "VisionFlow/inference/inference_error.hpp"
#include "core/expected_utils.hpp"

namespace vf {
OnnxDmlInferenceProcessor::OnnxDmlInferenceProcessor(
    InferenceConfig config, std::unique_ptr<FrameSequencer<InferenceFrame>> frameSequencer,
    InferenceResultStore* resultStore, std::unique_ptr<IInferenceSession> session,
    std::unique_ptr<IInferenceImageProcessor> dmlImageProcessor,
    std::unique_ptr<InferencePostprocessor> inferencePostprocessor,
    std::unique_ptr<DmlInferenceWorker<InferenceFrame>> inferenceWorker, IProfiler* profiler)
    : config(std::move(config)), frameSequencer(std::move(frameSequencer)),
      resultStore(resultStore), session(std::move(session)),
      dmlImageProcessor(std::move(dmlImageProcessor)),
      inferencePostprocessor(std::move(inferencePostprocessor)),
      inferenceWorker(std::move(inferenceWorker)), profiler(profiler) {
    if (this->inferenceWorker != nullptr) {
        this->inferenceWorker->setFaultHandler(
            [this](std::string_view reason, std::error_code errorCode) {
                transitionToFault(reason, errorCode);
            });
    }
}

OnnxDmlInferenceProcessor::~OnnxDmlInferenceProcessor() noexcept {
    try {
        const std::expected<void, std::error_code> stopResult = stop();
        static_cast<void>(stopResult);
    } catch (...) {
        static_cast<void>(0);
    }
}

std::expected<void, std::error_code> OnnxDmlInferenceProcessor::start() {
    {
        std::scoped_lock lock(stateMutex);
        if (state == ProcessorState::Running) {
            return {};
        }
        if (state == ProcessorState::Starting || state == ProcessorState::Stopping) {
            return std::unexpected(makeErrorCode(InferenceError::InvalidState));
        }
        state = ProcessorState::Starting;
    }

    if (frameSequencer == nullptr || resultStore == nullptr || session == nullptr ||
        dmlImageProcessor == nullptr || inferencePostprocessor == nullptr ||
        inferenceWorker == nullptr) {
        {
            std::scoped_lock lock(stateMutex);
            state = ProcessorState::Fault;
            lastError = makeErrorCode(InferenceError::InvalidState);
        }
        return std::unexpected(makeErrorCode(InferenceError::InvalidState));
    }

    frameSequence.store(0, std::memory_order_release);
    frameSequencer->startAccepting();
    workerThread =
        std::jthread([this](const std::stop_token& stopToken) { inferenceLoop(stopToken); });

    {
        std::scoped_lock lock(stateMutex);
        state = ProcessorState::Running;
        lastError.clear();
    }

    VF_INFO("OnnxDmlInferenceProcessor started");
    return {};
}

std::expected<void, std::error_code> OnnxDmlInferenceProcessor::stop() {
    {
        std::scoped_lock lock(stateMutex);
        if (state == ProcessorState::Idle) {
            return {};
        }
        state = ProcessorState::Stopping;
    }

    frameSequencer->stopAccepting();
    if (workerThread.joinable()) {
        workerThread.request_stop();
        workerThread.join();
    }
    frameSequencer->clear();

    {
        std::scoped_lock lock(stateMutex);
        state = ProcessorState::Idle;
        lastError.clear();
    }

    VF_INFO("OnnxDmlInferenceProcessor stopped");
    return {};
}

std::expected<void, std::error_code> OnnxDmlInferenceProcessor::poll() {
    std::scoped_lock lock(stateMutex);
    return pollFaultState(
        state == ProcessorState::Fault,
        FaultPollErrors{.lastError = lastError,
                        .fallbackError = makeErrorCode(InferenceError::InvalidState)});
}

void OnnxDmlInferenceProcessor::transitionToFault(std::string_view reason,
                                                  std::error_code errorCode) {
    {
        std::scoped_lock lock(stateMutex);
        state = ProcessorState::Fault;
        lastError = errorCode;
    }
    VF_ERROR("{}: {}", reason, errorCode.message());
}

void OnnxDmlInferenceProcessor::onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) {
    if (texture == nullptr) {
        return;
    }

    {
        std::scoped_lock lock(stateMutex);
        if (state != ProcessorState::Running) {
            return;
        }
    }

    const std::uint64_t fenceValue = frameSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    InferenceFrame frame;
    frame.texture.copy_from(texture);
    frame.info = info;
    frame.fenceValue = fenceValue;
    frameSequencer->submit(std::move(frame));
}

void OnnxDmlInferenceProcessor::inferenceLoop(const std::stop_token& stopToken) {
    if (inferenceWorker == nullptr) {
        transitionToFault("OnnxDmlInferenceProcessor runtime component is missing",
                          makeErrorCode(InferenceError::InvalidState));
        return;
    }

    inferenceWorker->run(stopToken);
}

} // namespace vf
