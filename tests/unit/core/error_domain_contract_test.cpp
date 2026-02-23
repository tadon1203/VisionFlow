#include "VisionFlow/core/config_error.hpp"
#include "VisionFlow/core/error_domain.hpp"
#include "VisionFlow/input/mouse_error.hpp"

namespace vf {
namespace {

static_assert(ErrorDomainEnum<ConfigError>);
static_assert(ErrorDomainEnum<MouseError>);
static_assert(HasErrorDomainTraits<ConfigError>);
static_assert(HasErrorDomainTraits<MouseError>);

} // namespace
} // namespace vf
