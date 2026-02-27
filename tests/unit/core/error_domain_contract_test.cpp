#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/config_error.hpp"
#include "VisionFlow/core/error_domain.hpp"
#include "VisionFlow/inference/inference_error.hpp"
#include "VisionFlow/input/mouse_error.hpp"

namespace vf {
namespace {

static_assert(ErrorDomainEnum<ConfigError>);
static_assert(ErrorDomainEnum<CaptureError>);
static_assert(ErrorDomainEnum<InferenceError>);
static_assert(ErrorDomainEnum<MouseError>);
static_assert(HasErrorDomainTraits<ConfigError>);
static_assert(HasErrorDomainTraits<CaptureError>);
static_assert(HasErrorDomainTraits<InferenceError>);
static_assert(HasErrorDomainTraits<MouseError>);

} // namespace
} // namespace vf
