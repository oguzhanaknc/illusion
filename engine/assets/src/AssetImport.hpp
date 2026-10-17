#pragma once

// Internal: the import-cache building blocks shared by AssetDatabase (synchronous) and AssetStreamer
// (asynchronous). Every function is thread-safe; file access goes through a VirtualFileSystem, whose
// reads are safe from any thread.

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetFormat.hpp>

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
inline constexpr std::string_view kImageImporter = "image:1";
inline constexpr std::string_view kModelImporter = "gltf:1";

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

// Decodes `source` (the bytes of `path`), builds mips if requested and fills `cooked`.
[[nodiscard]] std::optional<Image> importImage(std::span<const std::byte> source, const std::string& path,
                                               const ImageImportSettings& settings, AssetFile& cooked,
                                               std::string* error);

// Imports the glTF model at `path` (reading its files) and fills `cooked` (empty payload if a source
// could not be re-read for hashing, which only disables caching).
[[nodiscard]] std::optional<ModelData> importModel(const VirtualFileSystem& files, std::string_view path,
                                                   AssetFile& cooked, std::string* error);

} // namespace detail
} // namespace illusion
