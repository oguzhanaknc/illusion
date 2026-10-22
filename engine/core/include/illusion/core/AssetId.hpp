#pragma once

/// @file
/// Asset paths and their ids.
///
/// An asset is named by a virtual path (VirtualPath.hpp), optionally followed by a fragment that selects a
/// part of the file:
///
/// | Path | Asset |
/// | --- | --- |
/// | `game:/textures/rock.png` | a whole file |
/// | `game:/models/ship.glb#mesh/2` | mesh 2 of a model |
/// | `game:/models/ship.glb#material/0` | material 0 of a model |
/// | `game:/models/ship.glb#node/Hull` | the mesh of the model node named "Hull" |
/// | `builtin:/mesh/cube` | an asset the engine provides (see builtinAssetPaths()) |
///
/// The AssetId of an asset is the 64-bit FNV-1a hash of its normalized path, so the same path gives the
/// same id in every process and on every platform (ADR 0019, ADR 0035).

#include <illusion/core/Base.hpp>

#include <compare>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace illusion
{

/// Mount name reserved for the engine's built-in assets; VirtualFileSystem refuses to mount it.
inline constexpr std::string_view kBuiltinAssetMount = "builtin";

/// Stable identifier of an asset: the 64-bit FNV-1a hash of its normalized path.
struct AssetId
{
    u64 value = 0; ///< Hash value; 0 means "no asset".

    /// The id of the asset at @p path (see normalizeAssetPath()). Returns an empty id for a malformed path.
    /// Does not allocate when @p path is already normalized.
    [[nodiscard]] static AssetId fromPath(std::string_view path);

    /// True for a non-empty id.
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }

    /// Ids compare by value.
    friend auto operator<=>(const AssetId&, const AssetId&) noexcept = default;
};

/// The part of a file that an asset path selects.
enum class AssetFragment : u8
{
    None,     ///< The whole file.
    Mesh,     ///< `#mesh/N`: mesh N of a model.
    Material, ///< `#material/N`: material N of a model.
    Node,     ///< `#node/<name>`: the mesh of the first model node with that name.
};

/// A normalized asset path split into the file and the fragment. The views point into the path.
struct AssetPathParts
{
    std::string_view file;                        ///< Virtual path of the file (or built-in asset).
    AssetFragment fragment = AssetFragment::None; ///< Selected part.
    u32 index = 0;                                ///< Mesh, Material: the index.
    std::string_view node;                        ///< Node: the node name.
};

/// Normalizes the file part of @p path like normalizeVirtualPath() and writes the index of a `#mesh/N` or
/// `#material/N` fragment in decimal without leading zeros. Returns std::nullopt for a malformed path or
/// fragment (an unknown fragment kind, an index above 2^32 - 1, an empty or control-character node name).
[[nodiscard]] std::optional<std::string> normalizeAssetPath(std::string_view path);

/// True if @p path is a valid asset path in normalized form (normalizeAssetPath() returns it unchanged).
[[nodiscard]] bool isNormalizedAssetPath(std::string_view path) noexcept;

/// Splits the normalized asset path @p path. Returns std::nullopt unless isNormalizedAssetPath(@p path).
[[nodiscard]] std::optional<AssetPathParts> splitAssetPath(std::string_view path) noexcept;

/// Paths of the built-in assets, which need no files: the meshes `builtin:/mesh/{cube, sphere, plane,
/// capsule, cylinder}`, the material `builtin:/material/default` and the environment
/// `builtin:/sky/procedural`.
[[nodiscard]] std::span<const std::string_view> builtinAssetPaths() noexcept;

/// True if the normalized @p path is on the `builtin:` mount.
[[nodiscard]] bool isOnBuiltinMount(std::string_view path) noexcept;

/// True if the normalized @p path names a built-in asset (one of builtinAssetPaths()).
[[nodiscard]] bool isBuiltinAsset(std::string_view path) noexcept;

} // namespace illusion
