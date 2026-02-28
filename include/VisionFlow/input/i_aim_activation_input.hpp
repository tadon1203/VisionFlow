#pragma once

namespace vf {

class IAimActivationInput {
  public:
    IAimActivationInput() = default;
    IAimActivationInput(const IAimActivationInput&) = default;
    IAimActivationInput(IAimActivationInput&&) = default;
    IAimActivationInput& operator=(const IAimActivationInput&) = default;
    IAimActivationInput& operator=(IAimActivationInput&&) = default;
    virtual ~IAimActivationInput() = default;

    [[nodiscard]] virtual bool isAimActivationPressed() const = 0;
};

} // namespace vf
