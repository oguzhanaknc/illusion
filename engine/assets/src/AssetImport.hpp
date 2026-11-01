#pragma once

// Internal: the import-cache building blocks shared by AssetDatabase (synchronous) and AssetStreamer
// (asynchronous). Every function is thread-safe; file access goes through a VirtualFileSystem, whose
// reads are safe from any thread.

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/ImportSettings.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class VirtualFileSystem;

namespace detail
{

// Bump an importer's version when its output changes for the same input, to invalidate old caches.
// Version 2 records the import settings sidecar as a dependency.
inline constexpr std::string_view kImageImporter = "image:2";
inline constexpr std::string_view kModelImporter = "gltf:2";

[[nodiscard]] std::string imageImportKey(const ImageImportSettings& settings);

// Virtual path of the cooked file for `virtualPath` imported with `importKey`.
[[nodiscard]] std::string assetCachePath(std::string_view cacheMount, std::string_view virtualPath,
                                         std::string_view importKey);

// Parses a cached file; std::nullopt unless it is a well-formed `type` file of `version` with sources.
[[nodiscard]] std::optional<AssetFile> parseCache(std::span<const std::byte> bytes, AssetType type,
                                                  u32 version);

// True if `contents[i]` (the current bytes of file.dependencies[i]) all match the recorded hashes.
[[nodiscard]] bool dependenciesUnchanged(const AssetFile& file,
                                         std::span<const std::vector<std::byte>> contents);

// Reads the dependency `path` of a cooked file into `out`. A missing import settings sidecar reads as empty
// (no sidecar), so adding one makes the entry stale; any other missing file is a failure.
[[nodiscard]] bool readDependency(const VirtualFileSystem& files, const std::string& path,
                                  std::vector<std::byte>& out);

// Reads the sidecar of `assetPath` into `out` (empty if there is none). False (and `error`) if it exists
// but cannot be read.
[[nodiscard]] bool readImportSettingsFile(const VirtualFileSystem& files, std::string_view assetPath,
                                          std::vector<std::byte>& out, std::string* error);

// Parses `sidecar` (the bytes of the sidecar of `assetPath`; empty means defaults).
[[nodiscard]] std::optional<ImportSettings> parseImportSettingsFile(std::span<const std::byte> sidecar,
                                                                    std::string_view assetPath,
                                                                    ImportSettingsKind kind,
                                                                    std::string* error);

// Decodes `source` (the bytes of `path`) with `settings` changed by `sidecar` (the bytes of its import
// settings sidecar, empty if none), builds mips if requested and fills `cooked`.
[[nodiscard]] std::optional<Image> importImage(std::span<const std::byte> source,
                                               std::span<const std::byte> sidecar, const std::string& path,
                                               const ImageImportSettings& settings, AssetFile& cooked,
                                               std::string* error);

// Imports the glTF model at `path` (reading its files and its import settings sidecar) and fills `cooked`
// (empty payload if a source could not be re-read for hashing, which only disables caching).
[[nodiscard]] std::optional<ModelData> importModel(const VirtualFileSystem& files, std::string_view path,
                                                   AssetFile& cooked, std::string* error);

} // namespace detail
} // namespace illusion
