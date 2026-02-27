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
    InferenceResultStore* resultStore, std::unique_ptr<OnnxDmlSession> session,
    std::unique_ptr<DmlImageProcessor> dmlImageProcessor,
    std::unique_ptr<DmlInferenceWorker<InferenceFrame>> inferenceWorker)
    : config(std::move(config)), frameSequencer(std::move(frameSequencer)),
      resultStore(resultStore), session(std::move(session)),
      dmlImageProcessor(std::move(dmlImageProcessor)), inferenceWorker(std::move(inferenceWorker)) {
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
#if !defined(_WIN32) || !defined(VF_HAS_ONNXRUNTIME_DML) || !VF_HAS_ONNXRUNTIME_DML
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
#else
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
        dmlImageProcessor == nullptr || inferenceWorker == nullptr) {
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
#endif
}

std::expected<void, std::error_code> OnnxDmlInferenceProcessor::stop() {
#if !defined(_WIN32) || !defined(VF_HAS_ONNXRUNTIME_DML) || !VF_HAS_ONNXRUNTIME_DML
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
#else
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

    std::error_code stopError;
    if (session != nullptr) {
        const std::expected<void, std::error_code> sessionStopResult = session->stop();
        if (!sessionStopResult) {
            stopError = sessionStopResult.error();
        }
    }

    if (dmlImageProcessor != nullptr) {
        dmlImageProcessor->shutdown();
    }

    {
        std::scoped_lock lock(stateMutex);
        state = stopError ? ProcessorState::Fault : ProcessorState::Idle;
        lastError = stopError;
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    VF_INFO("OnnxDmlInferenceProcessor stopped");
    return {};
#endif
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
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    if (inferenceWorker == nullptr) {
        transitionToFault("OnnxDmlInferenceProcessor runtime component is missing",
                          makeErrorCode(InferenceError::InvalidState));
        return;
    }

    inferenceWorker->run(stopToken);
#else
    static_cast<void>(stopToken);
#endif
}

} // namespace vf
