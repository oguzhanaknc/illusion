#pragma once

/// @file
/// Semantic versions (https://semver.org, version 2.0.0) for the engine and its modules.

#include <illusion/core/Base.hpp>

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace illusion
{

/// A semantic version: MAJOR.MINOR.PATCH with an optional pre-release ("1.4.0-beta.2"). Build metadata
/// ("+abc") is accepted by parse() and ignored, as precedence rules require.
struct SemanticVersion
{
    u32 major = 0;          ///< Incompatible API changes.
    u32 minor = 0;          ///< Backwards-compatible additions.
    u32 patch = 0;          ///< Backwards-compatible fixes.
    std::string prerelease; ///< Dot-separated identifiers, empty for a release.

    /// Parses @p text strictly (no leading zeros, no "v" prefix). Returns nullopt if it is invalid.
    [[nodiscard]] static std::optional<SemanticVersion> parse(std::string_view text);

    /// Formats the version, e.g. "1.4.0-beta.2".
    [[nodiscard]] std::string toString() const;

    /// True if code built against @p required can use this version: the same major version (for 0.x,
    /// the same minor version, since anything may change before 1.0) and not older than @p required.
    [[nodiscard]] bool isCompatibleWith(const SemanticVersion& required) const;

    /// Orders versions by semver precedence (a pre-release is older than its release).
    friend std::strong_ordering operator<=>(const SemanticVersion& a, const SemanticVersion& b);

    /// Equal precedence (build metadata is never stored).
    friend bool operator==(const SemanticVersion& a, const SemanticVersion& b) = default;
};

/// The version of the engine, from the CMake project version.
[[nodiscard]] const SemanticVersion& engineVersion() noexcept;

} // namespace illusion
