#include "inference/engine/onnx_dml_inference_processor.hpp"

#include <expected>
#include <memory>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"

namespace vf {
std::expected<std::unique_ptr<OnnxDmlCaptureProcessor>, std::error_code>
OnnxDmlCaptureProcessor::createDefault(InferenceConfig config, IInferenceResultStore* resultStore) {
    if (resultStore == nullptr) {
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    try {
        auto sequencer = std::make_unique<FrameSequencer<WinrtCaptureFrame>>();
        auto dmlSession = std::make_unique<OnnxDmlSession>(config.modelPath);
        auto imageProcessor = std::make_unique<DmlImageProcessor>(*dmlSession);
        auto worker = std::make_unique<DmlInferenceWorker<WinrtCaptureFrame>>(
            sequencer.get(), dmlSession.get(), imageProcessor.get(), resultStore);

        return std::make_unique<OnnxDmlCaptureProcessor>(
            std::move(config), std::move(sequencer), resultStore, std::move(dmlSession),
            std::move(imageProcessor), std::move(worker));
    } catch (...) {
        VF_ERROR("OnnxDmlCaptureProcessor createDefault failed during component construction");
        return std::unexpected(makeErrorCode(CaptureError::InferenceInitializationFailed));
    }
}

OnnxDmlCaptureProcessor::OnnxDmlCaptureProcessor(
    InferenceConfig config, std::unique_ptr<FrameSequencer<WinrtCaptureFrame>> frameSequencer,
    IInferenceResultStore* resultStore, std::unique_ptr<OnnxDmlSession> session,
    std::unique_ptr<DmlImageProcessor> dmlImageProcessor,
    std::unique_ptr<DmlInferenceWorker<WinrtCaptureFrame>> inferenceWorker)
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

OnnxDmlCaptureProcessor::~OnnxDmlCaptureProcessor() noexcept {
    try {
        const std::expected<void, std::error_code> stopResult = stop();
        static_cast<void>(stopResult);
    } catch (...) {
        static_cast<void>(0);
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

    if (frameSequencer == nullptr || resultStore == nullptr || session == nullptr ||
        dmlImageProcessor == nullptr || inferenceWorker == nullptr) {
        {
            std::scoped_lock lock(stateMutex);
            state = ProcessorState::Fault;
        }
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    frameSequence.store(0, std::memory_order_release);
    frameSequencer->startAccepting();
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
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    VF_INFO("OnnxDmlCaptureProcessor stopped");
    return {};
#endif
}

void OnnxDmlCaptureProcessor::transitionToFault(std::string_view reason,
                                                std::error_code errorCode) {
    {
        std::scoped_lock lock(stateMutex);
        state = ProcessorState::Fault;
    }
    VF_ERROR("{}: {}", reason, errorCode.message());
}

void OnnxDmlCaptureProcessor::onFrame(ID3D11Texture2D* texture, const CaptureFrameInfo& info) {
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
    WinrtCaptureFrame frame;
    frame.texture.copy_from(texture);
    frame.info = info;
    frame.fenceValue = fenceValue;
    frameSequencer->submit(std::move(frame));
}

void OnnxDmlCaptureProcessor::inferenceLoop(const std::stop_token& stopToken) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    if (inferenceWorker == nullptr) {
        transitionToFault("OnnxDmlCaptureProcessor runtime component is missing",
                          makeErrorCode(CaptureError::InvalidState));
        return;
    }

    inferenceWorker->run(stopToken);
#else
    static_cast<void>(stopToken);
#endif
}

} // namespace vf
