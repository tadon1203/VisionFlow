#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace vf {

template <typename errorEnum>
concept ErrorDomainEnum = std::is_enum_v<errorEnum> && std::is_error_code_enum_v<errorEnum> &&
                          std::same_as<std::underlying_type_t<errorEnum>, std::uint8_t>;

template <typename errorEnum> struct ErrorDomainTraits;

template <typename errorEnum>
concept HasErrorDomainTraits = requires(errorEnum error) {
    { ErrorDomainTraits<errorEnum>::domainName() } -> std::convertible_to<const char*>;
    { ErrorDomainTraits<errorEnum>::unknownMessage() } -> std::convertible_to<std::string_view>;
    { ErrorDomainTraits<errorEnum>::message(error) } -> std::convertible_to<std::string_view>;
};

template <typename errorEnum>
concept StrictErrorDomain = ErrorDomainEnum<errorEnum> && HasErrorDomainTraits<errorEnum>;

template <ErrorDomainEnum errorEnum>
    requires HasErrorDomainTraits<errorEnum>
class ErrorDomainCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override {
        return ErrorDomainTraits<errorEnum>::domainName();
    }

    [[nodiscard]] std::string message(int value) const override {
        const auto error = static_cast<errorEnum>(value);
        const std::string_view text = ErrorDomainTraits<errorEnum>::message(error);
        if (!text.empty()) {
            return std::string(text);
        }
        return std::string(ErrorDomainTraits<errorEnum>::unknownMessage());
    }
};

template <ErrorDomainEnum errorEnum>
    requires HasErrorDomainTraits<errorEnum>
[[nodiscard]] const std::error_category& errorCategory() noexcept {
    static const ErrorDomainCategory<errorEnum> kCategory;
    return kCategory;
}

template <ErrorDomainEnum errorEnum>
    requires HasErrorDomainTraits<errorEnum>
[[nodiscard]] std::error_code makeErrorCode(errorEnum error) noexcept {
    return {static_cast<int>(error), errorCategory<errorEnum>()};
}

} // namespace vf
