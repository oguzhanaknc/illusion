#pragma once

/// @file
/// Cooking: importing every asset of a directory ahead of time, so a game can ship the import cache
/// (packed into an archive, see Archive.hpp) instead of the source files.

#include <illusion/core/Base.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class VirtualFileSystem;

/// Outcome of cookAssets().
struct CookResult
{
    u32 images = 0;                  ///< Images cooked (default import settings).
    u32 models = 0;                  ///< glTF models cooked (with the images they use).
    std::vector<std::string> errors; ///< One message per file that failed.
};

/// Imports every image (.png, .jpg, .jpeg, .tga, .bmp, .hdr) and glTF model (.gltf, .glb) under the
/// virtual directory @p directory (for example "game:/") into the import cache on @p cacheMount.
/// Standalone images are cooked with the default ImageImportSettings; other settings are imported on
/// first use. Files are visited in sorted order, so the result is deterministic.
[[nodiscard]] CookResult cookAssets(VirtualFileSystem& files, std::string_view directory,
                                    std::string_view cacheMount);

} // namespace illusion
