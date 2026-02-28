#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>

#include "VisionFlow/inference/inference_result.hpp"

#ifdef _WIN32
struct ID3D12Resource;
#endif

namespace vf {

class IInferenceSession {
  public:
    IInferenceSession() = default;
    IInferenceSession(const IInferenceSession&) = delete;
    IInferenceSession(IInferenceSession&&) = delete;
    IInferenceSession& operator=(const IInferenceSession&) = delete;
    IInferenceSession& operator=(IInferenceSession&&) = delete;
    virtual ~IInferenceSession() = default;

#ifdef _WIN32
    [[nodiscard]] virtual std::expected<InferenceResult, std::error_code>
    runWithGpuInput(std::int64_t frameTimestamp100ns, ID3D12Resource* resource,
                    std::size_t resourceBytes) = 0;
#else
    [[nodiscard]] virtual std::expected<InferenceResult, std::error_code>
    runWithGpuInput(std::int64_t frameTimestamp100ns, void* resource,
                    std::size_t resourceBytes) = 0;
#endif
};

} // namespace vf
