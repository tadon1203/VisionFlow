#include <expected>
#include <system_error>

#include "VisionFlow/inference/inference_error.hpp"
#include "inference/backend/dml/onnx_dml_session.hpp"

#if !defined(_WIN32) || !defined(VF_HAS_ONNXRUNTIME_DML) || !VF_HAS_ONNXRUNTIME_DML
namespace vf {

#ifdef _WIN32
std::expected<void, std::error_code> OnnxDmlSession::start(IDMLDevice* dmlDevice,
                                                           ID3D12CommandQueue* commandQueue,
                                                           std::uint64_t interopGeneration) {
    static_cast<void>(interopGeneration);
    static_cast<void>(dmlDevice);
    static_cast<void>(commandQueue);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code> OnnxDmlSession::start(IDMLDevice* dmlDevice,
                                                           ID3D12CommandQueue* commandQueue) {
    static_cast<void>(dmlDevice);
    static_cast<void>(commandQueue);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}
#else
std::expected<void, std::error_code> OnnxDmlSession::start(void* dmlDevice, void* commandQueue,
                                                           std::uint64_t interopGeneration) {
    static_cast<void>(interopGeneration);
    static_cast<void>(dmlDevice);
    static_cast<void>(commandQueue);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

std::expected<void, std::error_code> OnnxDmlSession::start(void* dmlDevice, void* commandQueue) {
    static_cast<void>(dmlDevice);
    static_cast<void>(commandQueue);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}
#endif

std::expected<void, std::error_code> OnnxDmlSession::stop() {
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}

#ifdef _WIN32
std::expected<InferenceResult, std::error_code>
OnnxDmlSession::runWithGpuInput(std::int64_t frameTimestamp100ns, ID3D12Resource* resource,
                                std::size_t resourceBytes) {
    static_cast<void>(frameTimestamp100ns);
    static_cast<void>(resource);
    static_cast<void>(resourceBytes);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}
#else
std::expected<InferenceResult, std::error_code>
OnnxDmlSession::runWithGpuInput(std::int64_t frameTimestamp100ns, void* resource,
                                std::size_t resourceBytes) {
    static_cast<void>(frameTimestamp100ns);
    static_cast<void>(resource);
    static_cast<void>(resourceBytes);
    return std::unexpected(makeErrorCode(InferenceError::PlatformNotSupported));
}
#endif

} // namespace vf
#endif
