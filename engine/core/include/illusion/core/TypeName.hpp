#pragma once

/// @file
/// Compile-time type names.
///
/// typeName<T>() returns the fully qualified name of T, e.g. "game::Position", with MSVC's
/// "struct "/"class "/"enum " prefixes removed. Names of plain (non-template) types are identical on all
/// supported compilers. Spelling of template arguments may differ between compilers, so never persist
/// these names; use explicit reflection names for serialization.

#include <illusion/core/Base.hpp>
#include <illusion/core/Hash.hpp>

#include <string_view>

namespace illusion
{
namespace detail
{

template <class T>
constexpr std::string_view rawTypeSignature() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

// The signature of rawTypeSignature<double>() tells us how much text surrounds the type name.
inline constexpr std::string_view kProbeSignature = rawTypeSignature<double>();
inline constexpr usize kTypeNamePrefix = kProbeSignature.find("double");
inline constexpr usize kTypeNameSuffix =
    kProbeSignature.size() - kTypeNamePrefix - std::string_view("double").size();
static_assert(kTypeNamePrefix != std::string_view::npos, "Unsupported compiler for typeName<T>()");

constexpr std::string_view stripElaboratedPrefix(std::string_view name) noexcept
{
    for (const std::string_view prefix : {std::string_view("struct "), std::string_view("class "),
                                          std::string_view("enum "), std::string_view("union ")})
    {
        if (name.starts_with(prefix))
        {
            return name.substr(prefix.size());
        }
    }
    return name;
}

} // namespace detail

/// Returns the fully qualified name of @p T. See the file documentation for portability notes.
template <class T>
[[nodiscard]] constexpr std::string_view typeName() noexcept
{
    constexpr std::string_view signature = detail::rawTypeSignature<T>();
    constexpr std::string_view name = signature.substr(
        detail::kTypeNamePrefix, signature.size() - detail::kTypeNamePrefix - detail::kTypeNameSuffix);
    return detail::stripElaboratedPrefix(name);
}

/// Returns fnv1a64(typeName<T>()).
template <class T>
[[nodiscard]] constexpr u64 typeHash() noexcept
{
    return fnv1a64(typeName<T>());
}

} // namespace illusion
