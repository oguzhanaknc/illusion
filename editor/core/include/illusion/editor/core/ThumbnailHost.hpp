#pragma once

/// @file
/// What `asset.thumbnail` needs from the GPU: square preview images of models, material files, textures and
/// environments. The thumbnail renderer of the editor process (`illusion::editor_viewport`) implements it and
/// registers with EditorContext::setThumbnailHost(); without one (an editor without a GPU), thumbnails that
/// are not cached yet fail with ErrorKind::Unsupported. The PNG files are kept by ThumbnailCache.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/CommandError.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// Width and height of a thumbnail in pixels.
inline constexpr u32 kThumbnailSize = 256;

/// What a thumbnail shows.
enum class ThumbnailKind : u8
{
    Model,       ///< A glTF model (`.gltf`, `.glb`), framed and lit in a preview World.
    Material,    ///< A `.material` file on a sphere, lit in a preview World.
    Texture,     ///< An image (`.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`), scaled down over a checkerboard.
    Environment, ///< An equirectangular `.hdr` image as the background of a preview World, with a sphere.
};

/// The thumbnail kind of the asset at @p path, decided by its extension in any case (std::nullopt for files
/// without thumbnails).
[[nodiscard]] std::optional<ThumbnailKind> thumbnailKindOf(std::string_view path);

/// The name of @p kind ("Model", "Material", "Texture" or "Environment").
[[nodiscard]] std::string_view thumbnailKindName(ThumbnailKind kind) noexcept;

/// A thumbnail to make.
struct ThumbnailRequest
{
    std::string path;                          ///< Virtual path of the asset, e.g. "assets:/models/ship.glb".
    ThumbnailKind kind = ThumbnailKind::Model; ///< What to show (thumbnailKindOf() of the path).
};

/// A finished thumbnail.
struct ThumbnailImage
{
    UVec2 size{0};        ///< Size in pixels (kThumbnailSize square).
    std::vector<u8> rgba; ///< sRGB-encoded RGBA8 rows, top row first, fully opaque.
};

/// The renderer behind `asset.thumbnail`. Called on the main thread; requests are made one at a time in the
/// order they arrive, at most one per frame.
class ThumbnailHost
{
public:
    virtual ~ThumbnailHost() = default;

    /// Queues @p request (validated by the command: an existing asset of that kind) and returns a ticket for
    /// thumbnail().
    [[nodiscard]] virtual Expected<u64, CommandError> requestThumbnail(const ThumbnailRequest& request) = 0;

    /// The result of @p ticket once it is made (std::nullopt while pending): the image, or why the asset
    /// could not be shown (ErrorKind::IoError for files that do not load). Each ticket is answered once.
    [[nodiscard]] virtual std::optional<Expected<ThumbnailImage, CommandError>> thumbnail(u64 ticket) = 0;

    /// Forgets @p ticket (a cancelled command); unknown and answered tickets are ignored.
    virtual void cancelThumbnail(u64 ticket) = 0;
};

} // namespace illusion::editor
