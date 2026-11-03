#pragma once

/// @file
/// What the asset browser needs from the GPU to show images of files: the PNG files `asset.thumbnail`
/// makes, loaded from the virtual file system and uploaded as UI textures. The editor process
/// (ViewportService in `illusion::editor_viewport`) implements it and registers with
/// EditorContext::setImageHost(); without one (an editor without a GPU), every image is unavailable.

#include <illusion/core/Base.hpp>

#include <string_view>

namespace illusion::editor
{

/// The images the UI shows besides its icons. Use on the main thread.
class ImageHost
{
public:
    virtual ~ImageHost() = default;

    /// The UI texture of the image at the virtual path @p path (e.g. "thumbnails:/0123456789abcdef.png"),
    /// loading and uploading it on first use. 0 while the file is missing or cannot be decoded, and for
    /// the rest of the session unless forgetImage() says otherwise. The texture stays alive until then.
    [[nodiscard]] virtual u64 imageTexture(std::string_view path) = 0;

    /// Forgets the texture of @p path so the next imageTexture() loads the file again; "" forgets every
    /// image (after the project changed, say).
    virtual void forgetImage(std::string_view path) = 0;
};

} // namespace illusion::editor
