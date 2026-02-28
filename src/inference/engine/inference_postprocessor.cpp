#include "inference/engine/inference_postprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#include "VisionFlow/inference/inference_error.hpp"

namespace vf {

namespace {

struct CandidateDetection {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float score = 0.0F;
    std::int32_t classId = 0;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
};

[[nodiscard]] bool isFiniteAndPositive(float value) noexcept {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] bool isFiniteScore(float value) noexcept { return std::isfinite(value); }

[[nodiscard]] float computeIou(const CandidateDetection& left,
                               const CandidateDetection& right) noexcept {
    const float intersectionX1 = std::max(left.x1, right.x1);
    const float intersectionY1 = std::max(left.y1, right.y1);
    const float intersectionX2 = std::min(left.x2, right.x2);
    const float intersectionY2 = std::min(left.y2, right.y2);

    const float intersectionWidth = std::max(0.0F, intersectionX2 - intersectionX1);
    const float intersectionHeight = std::max(0.0F, intersectionY2 - intersectionY1);
    const float intersectionArea = intersectionWidth * intersectionHeight;

    const float leftArea = std::max(0.0F, left.x2 - left.x1) * std::max(0.0F, left.y2 - left.y1);
    const float rightArea =
        std::max(0.0F, right.x2 - right.x1) * std::max(0.0F, right.y2 - right.y1);
    const float unionArea = leftArea + rightArea - intersectionArea;
    if (unionArea <= std::numeric_limits<float>::epsilon()) {
        return 0.0F;
    }

    return intersectionArea / unionArea;
}

[[nodiscard]] std::expected<const InferenceTensor*, std::error_code>
findOutputTensor(const InferenceResult& result, const std::string& outputTensorName) {
    const auto it = std::find_if(
        result.tensors.begin(), result.tensors.end(),
        [&](const InferenceTensor& tensor) { return tensor.name == outputTensorName; });
    if (it == result.tensors.end()) {
        return std::unexpected(makeErrorCode(InferenceError::ModelInvalid));
    }
    return &(*it);
}

[[nodiscard]] std::expected<void, std::error_code>
validateTensorLayout(const InferenceTensor& tensor, const std::array<int64_t, 3>& expectedShape) {
    if (tensor.shape.size() != expectedShape.size()) {
        return std::unexpected(makeErrorCode(InferenceError::ModelInvalid));
    }

    for (std::size_t i = 0; i < expectedShape.size(); ++i) {
        if (tensor.shape.at(i) != expectedShape.at(i)) {
            return std::unexpected(makeErrorCode(InferenceError::ModelInvalid));
        }
    }

    const auto batch = static_cast<std::size_t>(expectedShape.at(0));
    const auto channels = static_cast<std::size_t>(expectedShape.at(1));
    const auto anchors = static_cast<std::size_t>(expectedShape.at(2));
    const auto expectedElementCount = batch * channels * anchors;
    if (tensor.values.size() != expectedElementCount) {
        return std::unexpected(makeErrorCode(InferenceError::RunFailed));
    }

    return {};
}

} // namespace

InferencePostprocessor::InferencePostprocessor() : InferencePostprocessor(Settings{}) {}

InferencePostprocessor::InferencePostprocessor(Settings settings) : settings(std::move(settings)) {}

std::expected<void, std::error_code>
InferencePostprocessor::process(InferenceResult& result) const {
    result.detections.clear();

    const auto outputTensorResult = findOutputTensor(result, settings.outputTensorName);
    if (!outputTensorResult) {
        return std::unexpected(outputTensorResult.error());
    }

    const InferenceTensor* outputTensor = outputTensorResult.value();
    const auto layoutValidationResult =
        validateTensorLayout(*outputTensor, settings.outputTensorShape);
    if (!layoutValidationResult) {
        return std::unexpected(layoutValidationResult.error());
    }

    const std::size_t anchors = static_cast<std::size_t>(settings.outputTensorShape.at(2));
    std::vector<CandidateDetection> candidates;
    candidates.reserve(anchors);

    for (std::size_t anchorIndex = 0; anchorIndex < anchors; ++anchorIndex) {
        const float centerX = outputTensor->values.at(anchorIndex);
        const float centerY = outputTensor->values.at(anchors + anchorIndex);
        const float width = outputTensor->values.at((2U * anchors) + anchorIndex);
        const float height = outputTensor->values.at((3U * anchors) + anchorIndex);
        const float score = outputTensor->values.at((4U * anchors) + anchorIndex);

        if (!isFiniteScore(score) || score < settings.confidenceThreshold) {
            continue;
        }
        if (!isFiniteAndPositive(width) || !isFiniteAndPositive(height) ||
            !std::isfinite(centerX) || !std::isfinite(centerY)) {
            continue;
        }

        constexpr std::int32_t kSingleClassId = 0;
        if (!isClassAllowed(kSingleClassId)) {
            continue;
        }

        CandidateDetection candidate;
        candidate.centerX = centerX;
        candidate.centerY = centerY;
        candidate.width = width;
        candidate.height = height;
        candidate.score = score;
        candidate.classId = kSingleClassId;
        candidate.x1 = centerX - (width * 0.5F);
        candidate.y1 = centerY - (height * 0.5F);
        candidate.x2 = centerX + (width * 0.5F);
        candidate.y2 = centerY + (height * 0.5F);
        candidates.emplace_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const CandidateDetection& left, const CandidateDetection& right) {
                  return left.score > right.score;
              });

    std::vector<CandidateDetection> selected;
    selected.reserve(std::min(settings.maxDetections, candidates.size()));
    for (const CandidateDetection& candidate : candidates) {
        bool keep = true;
        for (const CandidateDetection& kept : selected) {
            if (candidate.classId != kept.classId) {
                continue;
            }
            if (computeIou(candidate, kept) > settings.nmsIouThreshold) {
                keep = false;
                break;
            }
        }

        if (!keep) {
            continue;
        }

        selected.emplace_back(candidate);
        if (selected.size() >= settings.maxDetections) {
            break;
        }
    }

    result.detections.reserve(selected.size());
    for (const CandidateDetection& detection : selected) {
        result.detections.emplace_back(InferenceDetection{
            .centerX = detection.centerX,
            .centerY = detection.centerY,
            .width = detection.width,
            .height = detection.height,
            .score = detection.score,
            .classId = detection.classId,
        });
    }

    return {};
}

bool InferencePostprocessor::isClassAllowed(std::int32_t classId) const {
    const auto it =
        std::find(settings.allowedClassIds.begin(), settings.allowedClassIds.end(), classId);
    return it != settings.allowedClassIds.end();
}

} // namespace vf
