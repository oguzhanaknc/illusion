#pragma once

/// @file
/// Syntax of virtual paths (`mount:/relative/path`), checked and normalized without a file system.
///
/// VirtualFileSystem (illusion/platform/FileSystem.hpp) maps these paths to files; asset ids
/// (AssetId.hpp) hash their normalized form. A mount name is lowercase ASCII letters, digits, '_' and '-'.
/// The relative part uses '/' separators and may not contain empty, "." or ".." components, backslashes,
/// colons, control characters, components ending in '.' or ' ', or Windows device names, so a virtual
/// path can never escape its mount and means the same file on every platform.

#include <illusion/core/Base.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace illusion
{

/// True if @p name is a valid mount name: not empty; lowercase ASCII letters, digits, '_' and '-'.
[[nodiscard]] bool isValidMountName(std::string_view name) noexcept;

/// True if @p relative is a valid, normalized path below a mount (the part after "mount:/"). The empty
/// path, the mount root, is valid.
[[nodiscard]] bool isValidRelativePath(std::string_view relative) noexcept;

/// True if @p virtualPath is a valid virtual path in normalized form, i.e. normalizeVirtualPath() would
/// return it unchanged.
[[nodiscard]] bool isNormalizedVirtualPath(std::string_view virtualPath) noexcept;

/// Resolves "." and ".." components and removes empty ones ("game:/a/./b/../c" becomes "game:/a/c").
/// Returns std::nullopt for a malformed path or one whose ".." would leave the mount.
[[nodiscard]] std::optional<std::string> normalizeVirtualPath(std::string_view virtualPath);

} // namespace illusion
