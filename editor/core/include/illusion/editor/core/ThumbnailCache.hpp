#pragma once

/// @file
/// The PNG files of asset thumbnails in the project's `thumbnails:` mount (`.illusion/thumbnails`), named
/// after a key of the asset's contents, so a thumbnail is made once per version of an asset and found again
/// after a restart. See docs/adr/0045-asset-catalog-and-file-watching.md.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/editor/core/CommandError.hpp>

#include <map>
#include <string>
#include <string_view>

namespace illusion
{
class VirtualFileSystem;
} // namespace illusion

namespace illusion::editor
{

class AssetCatalog;
struct AssetCatalogChanges;
struct ThumbnailImage;

/// Version of the thumbnail images; part of every key, raised when the renderer draws them differently.
inline constexpr u32 kThumbnailVersion = 1;

/// The mount that holds the thumbnails.
inline constexpr std::string_view kThumbnailMount = "thumbnails";

/// The key of the thumbnail of @p path: a hash of kThumbnailVersion, the path's extension and the content and
/// import settings hashes of the asset and, recursively, of the assets it depends on (a material's textures,
/// a glTF file's buffers and images; a missing one counts as well). Never 0 for an asset in @p catalog; 0 for
/// paths the catalog does not know or folders.
[[nodiscard]] u64 thumbnailKey(const AssetCatalog& catalog, std::string_view path);

/// The virtual path of the thumbnail with @p key, e.g. "thumbnails:/0123456789abcdef.png".
[[nodiscard]] std::string thumbnailPath(u64 key);

/// Counters of a ThumbnailCache.
struct ThumbnailCacheStatistics
{
    u32 stored = 0;      ///< Thumbnails written since the cache was opened.
    u32 invalidated = 0; ///< Thumbnail files removed by invalidate() since the cache was opened.
};

/// The thumbnails on disk. Use on the main thread.
///
/// Keys change with the asset, so a stale file is never returned. invalidate() also removes the files this
/// cache made or found for assets that changed since (thumbnails of earlier editor sessions that nobody
/// asked for again stay until `.illusion/` is cleaned).
class ThumbnailCache
{
public:
    /// Uses the `thumbnails:` mount of @p files, which must outlive the cache or the next close().
    void open(VirtualFileSystem& files) noexcept;

    /// Forgets the project.
    void close() noexcept;

    /// True between open() and close().
    [[nodiscard]] bool isOpen() const noexcept { return m_files != nullptr; }

    /// True if the thumbnail with @p key is on disk; records that it belongs to @p asset.
    [[nodiscard]] bool find(std::string_view asset, u64 key);

    /// Encodes @p image as a PNG and writes it as the thumbnail of @p asset with @p key. Returns its virtual
    /// path, or ErrorKind::IoError (also without an open project).
    [[nodiscard]] Expected<std::string, CommandError> store(std::string_view asset, u64 key,
                                                            const ThumbnailImage& image);

    /// Removes the thumbnails of the assets in @p changes (modified, removed and the old paths of renames)
    /// and of the assets that refer to them in @p catalog. Returns the number of files removed.
    u32 invalidate(const AssetCatalogChanges& changes, const AssetCatalog& catalog);

    /// Counters.
    [[nodiscard]] const ThumbnailCacheStatistics& statistics() const noexcept { return m_statistics; }

private:
    void forget(const std::string& asset);

    VirtualFileSystem* m_files = nullptr;
    std::map<std::string, u64, std::less<>> m_known; // asset -> key of the file it has on disk
    ThumbnailCacheStatistics m_statistics;
};

} // namespace illusion::editor
