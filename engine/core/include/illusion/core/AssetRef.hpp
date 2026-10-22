#pragma once

/// @file
/// References to assets in Components and other reflected data, and the process-wide table that maps
/// their ids back to paths.
///
/// An AssetRef holds only an AssetId; the AssetPathTable remembers the path of every id it has seen, so
/// scene files, the editor and resolvers can turn the id back into a path. Shared modules use their
/// host's table (ModuleServices::assetPaths).
///
/// @code
/// illusion::MeshRenderer renderer;
/// renderer.mesh = illusion::AssetRef("game:/models/ship.glb#mesh/0");
/// renderer.material = illusion::AssetRef("game:/materials/hull.material");
/// ILL_LOG_INFO("Game", "drawing {}", renderer.mesh.path());
/// @endcode

#include <illusion/core/AssetId.hpp>
#include <illusion/core/Base.hpp>

#include <memory>
#include <string_view>

namespace illusion
{

/// Thread-safe, append-only map between asset ids and their normalized paths. Paths are never removed,
/// so the views path() returns stay valid as long as the table exists.
class AssetPathTable : NonCopyable
{
public:
    AssetPathTable();
    ~AssetPathTable();

    /// The active table (a built-in default unless setInstance() was called).
    [[nodiscard]] static AssetPathTable& instance() noexcept;

    /// Makes @p table the active table (nullptr restores the default). Used by shared modules to adopt
    /// their host's table (see ModuleServices).
    static void setInstance(AssetPathTable* table) noexcept;

    /// Normalizes @p path (normalizeAssetPath()), records it and returns its id. Returns an empty id for
    /// a malformed path, and logs an error and returns an empty id if a different path with the same id
    /// is already recorded (a 64-bit hash collision). Does not allocate for a recorded, normalized path.
    [[nodiscard]] AssetId intern(std::string_view path);

    /// The normalized path recorded for @p id, or an empty view for an empty or unknown id.
    [[nodiscard]] std::string_view path(AssetId id) const noexcept;

    /// Number of recorded paths.
    [[nodiscard]] usize size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// What an asset reference is expected to point to (FieldMeta::assetKind). Editors use it to filter
/// asset pickers; reading and resolving do not check it.
enum class AssetKind : u8
{
    Any,         ///< Any asset.
    Mesh,        ///< A mesh: `#mesh/N` or `#node/<name>` of a model, or `builtin:/mesh/...`.
    Material,    ///< A `.material` file, `#material/N` of a model, or `builtin:/material/default`.
    Texture,     ///< An image file.
    Environment, ///< An equirectangular image (usually `.hdr`) or `builtin:/sky/procedural`.
    AudioClip,   ///< A WAV, FLAC, MP3 or Ogg Vorbis file.
    Model,       ///< A glTF model (`.gltf`, `.glb`).
    Scene,       ///< A scene file.
};

/// English name of @p kind ("Mesh", "AudioClip", ...), for tools and schemas.
[[nodiscard]] std::string_view assetKindName(AssetKind kind) noexcept;

/// A reference to an asset: the reflected field type FieldType::Asset. Written to JSON as its path.
struct AssetRef
{
    AssetId id; ///< The asset; empty for "none".

    /// No asset.
    AssetRef() = default;

    /// Refers to @p asset, whose path should be recorded in AssetPathTable::instance().
    explicit AssetRef(AssetId asset) noexcept
        : id(asset)
    {
    }

    /// Refers to the asset at @p path, recorded in AssetPathTable::instance(). A malformed path gives an
    /// empty reference.
    explicit AssetRef(std::string_view path)
        : id(AssetPathTable::instance().intern(path))
    {
    }

    /// True if no asset is referenced.
    [[nodiscard]] bool empty() const noexcept { return !id; }

    /// True if an asset is referenced.
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(id); }

    /// The asset's path from AssetPathTable::instance() (empty for no asset).
    [[nodiscard]] std::string_view path() const noexcept { return AssetPathTable::instance().path(id); }

    /// References compare by id.
    friend bool operator==(const AssetRef&, const AssetRef&) noexcept = default;
};

} // namespace illusion
