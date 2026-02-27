#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "inference/platform/dml/onnx_dml_session.hpp"

namespace vf {

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

    if (inputShape.at(0) != 1) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (inputShape.at(1) != 3) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    if (outputNames.empty()) {
        return std::unexpected(makeErrorCode(CaptureError::InferenceModelInvalid));
    }

    ModelMetadata metadata;
    metadata.inputName = std::move(inputName);
    metadata.outputNames = std::move(outputNames);
    metadata.inputShape = std::move(inputShape);
    const auto batch = static_cast<std::size_t>(metadata.inputShape.at(0));
    const auto channels = static_cast<std::size_t>(metadata.inputShape.at(1));
    const auto height = static_cast<std::size_t>(metadata.inputShape.at(2));
    const auto width = static_cast<std::size_t>(metadata.inputShape.at(3));

    metadata.inputChannels = static_cast<std::uint32_t>(channels);
    metadata.inputHeight = static_cast<std::uint32_t>(height);
    metadata.inputWidth = static_cast<std::uint32_t>(width);
    metadata.inputElementCount = batch * channels * height * width;
    metadata.inputTensorBytes = metadata.inputElementCount * sizeof(float);

    return metadata;
}

OnnxDmlSession::~OnnxDmlSession() noexcept {
    try {
        const std::expected<void, std::error_code> stopResult = stop();
        if (!stopResult) {
            VF_WARN("OnnxDmlSession stop during destruction failed: {}",
                    stopResult.error().message());
        }
    } catch (...) {
        VF_WARN("OnnxDmlSession stop during destruction failed with unknown exception");
    }
}

std::filesystem::path OnnxDmlSession::resolveModelPath() const {
    if (modelPath.is_absolute()) {
        return modelPath;
    }
    return std::filesystem::current_path() / "models" / modelPath;
}

const OnnxDmlSession::ModelMetadata& OnnxDmlSession::metadata() const { return modelMetadata; }

} // namespace vf
