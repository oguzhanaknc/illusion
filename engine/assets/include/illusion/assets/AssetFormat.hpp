#pragma once

/// @file
/// Cooked asset files: a versioned container with a checksum and the source files an asset was imported
/// from, plus the binary formats of cooked images and models.

#include <illusion/assets/Image.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetId.hpp> // AssetId lives in core; included here for existing users
#include <illusion/core/Base.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// What a cooked asset file contains.
enum class AssetType : u32
{
    Image = 1, ///< serializeImage() payload.
    Model = 2, ///< serializeModel() payload.
};

/// A source file an asset was imported from, with the hash of its content at import time.
struct AssetDependency
{
    std::string path;    ///< Virtual path.
    u64 contentHash = 0; ///< xxhash64() of the file's bytes.

    /// Dependencies compare by path and hash.
    friend bool operator==(const AssetDependency&, const AssetDependency&) = default;
};

/// A cooked asset: type, payload format version, sources and payload.
struct AssetFile
{
    AssetType type = AssetType::Image;         ///< Payload type.
    u32 version = 0;                           ///< Payload format version (changes invalidate caches).
    std::vector<AssetDependency> dependencies; ///< Source files.
    std::vector<std::byte> payload;            ///< Serialized asset.
};

/// Payload format versions; bump when the serialized layout changes.
inline constexpr u32 kImageFormatVersion = 1;
/// @copydoc kImageFormatVersion
inline constexpr u32 kModelFormatVersion = 1;

/// Encodes @p file: magic, container version, type, version, dependencies, payload size and checksum,
/// payload.
[[nodiscard]] std::vector<std::byte> writeAssetFile(const AssetFile& file);

/// Decodes a file written by writeAssetFile(). Returns std::nullopt (and sets @p error) for wrong magic
/// or container version, truncated data or a checksum mismatch.
[[nodiscard]] std::optional<AssetFile> readAssetFile(std::span<const std::byte> bytes,
                                                     std::string* error = nullptr);

/// Serializes an image with all its mips.
[[nodiscard]] std::vector<std::byte> serializeImage(const Image& image);

/// Reads an image written by serializeImage(); returns std::nullopt for invalid data.
[[nodiscard]] std::optional<Image> deserializeImage(std::span<const std::byte> bytes);

/// Serializes a model (meshes, materials, images, nodes).
[[nodiscard]] std::vector<std::byte> serializeModel(const ModelData& model);

/// Reads a model written by serializeModel(); returns std::nullopt for invalid data (including indices
/// that point outside their arrays).
[[nodiscard]] std::optional<ModelData> deserializeModel(std::span<const std::byte> bytes);

} // namespace illusion
