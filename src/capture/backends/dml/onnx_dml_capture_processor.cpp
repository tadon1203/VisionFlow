#include "capture/backends/dml/onnx_dml_capture_processor.hpp"

#include <expected>
#include <memory>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/backends/dml/onnx_dml_session.hpp"

namespace vf {

OnnxDmlCaptureProcessor::OnnxDmlCaptureProcessor(InferenceConfig config)
    : config(std::move(config)) {}

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

    session = std::make_unique<OnnxDmlSession>(config.modelPath);
    dmlImageProcessor = std::make_unique<DmlImageProcessor>(*session);

    frameSequence.store(0, std::memory_order_release);
    frameSequencer.startAccepting();
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

    frameSequencer.stopAccepting();
    if (workerThread.joinable()) {
        workerThread.request_stop();
        workerThread.join();
    }
    frameSequencer.clear();

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

    dmlImageProcessor = nullptr;
    session.reset();

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
    frameSequencer.submit(texture, info, fenceValue);
}

void OnnxDmlCaptureProcessor::inferenceLoop(const std::stop_token& stopToken) {
#if defined(_WIN32) && defined(VF_HAS_ONNXRUNTIME_DML) && VF_HAS_ONNXRUNTIME_DML
    while (!stopToken.stop_requested()) {
        FrameSequencer::PendingFrame frame;
        if (!frameSequencer.waitAndTakeLatest(stopToken, frame)) {
            continue;
        }

        if (frame.texture == nullptr || session == nullptr || dmlImageProcessor == nullptr) {
            transitionToFault("OnnxDmlCaptureProcessor runtime component is missing",
                              makeErrorCode(CaptureError::InvalidState));
            return;
        }

        const std::expected<DmlImageProcessor::InitializeResult, std::error_code> initializeResult =
            dmlImageProcessor->initialize(frame.texture.get());
        if (!initializeResult) {
            transitionToFault("OnnxDmlCaptureProcessor GPU initialization failed",
                              initializeResult.error());
            return;
        }

        const std::expected<DmlImageProcessor::DispatchResult, std::error_code> dispatchResult =
            dmlImageProcessor->dispatch(frame.texture.get(), frame.fenceValue);
        if (!dispatchResult) {
            transitionToFault("OnnxDmlCaptureProcessor preprocess failed", dispatchResult.error());
            return;
        }

        const std::expected<InferenceResult, std::error_code> inferenceResult =
            session->runWithGpuInput(frame.info.systemRelativeTime100ns,
                                     dispatchResult->outputResource, dispatchResult->outputBytes);

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
