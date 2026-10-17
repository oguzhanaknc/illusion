#pragma once

/// @file
/// Loads source assets through an import cache: each source is imported (decoded, mip-mapped, converted)
/// once, the cooked result is stored with the content hashes of the files it came from, and later loads
/// read the cooked file as long as those files are unchanged.

#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/Base.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace illusion
{

class VirtualFileSystem;

/// How an image file is imported.
struct ImageImportSettings
{
    bool srgb = true;         ///< Decode 8-bit images as sRGB colour data (false for normal maps, masks).
    bool generateMips = true; ///< Build a full mip chain.
};

/// Counters of an AssetDatabase.
struct AssetDatabaseStatistics
{
    u32 cacheHits = 0;   ///< Loads served from the cache.
    u32 imports = 0;     ///< Loads that imported the source (cache miss or stale entry).
    u32 cacheWrites = 0; ///< Cooked files written.
};

/// Imports and caches assets. Not thread-safe.
class AssetDatabase : NonCopyable
{
public:
    /// Uses @p files for sources and the mount @p cacheMount (for example "cache") for cooked files. With
    /// an empty @p cacheMount every load imports from source. With @p verifySources false, cached files are
    /// used without checking their sources (shipped games that mount a cooked archive and no sources).
    AssetDatabase(VirtualFileSystem& files, std::string cacheMount, bool verifySources = true);

    /// Loads the image at @p virtualPath (PNG, JPEG, TGA, BMP, Radiance HDR).
    [[nodiscard]] std::optional<Image> loadImage(std::string_view virtualPath,
                                                 const ImageImportSettings& settings = {},
                                                 std::string* error = nullptr);

    /// Loads the glTF model at @p virtualPath (.gltf or .glb).
    [[nodiscard]] std::optional<ModelData> loadModel(std::string_view virtualPath,
                                                     std::string* error = nullptr);

    /// Virtual path of the cached file for an import of @p virtualPath with @p importKey (importer name,
    /// version and settings). Exposed for tools and tests.
    [[nodiscard]] std::string cachePath(std::string_view virtualPath, std::string_view importKey) const;

    /// Counters since construction.
    [[nodiscard]] const AssetDatabaseStatistics& statistics() const noexcept { return m_statistics; }

private:
    // Returns the cached file of `type`/`version` at `cachePath` if every source is unchanged.
    [[nodiscard]] std::optional<AssetFile> readCache(const std::string& cachePath, AssetType type,
                                                     u32 version) const;
    void writeCache(const std::string& cachePath, const AssetFile& file);

    VirtualFileSystem* m_files;
    std::string m_cacheMount;
    bool m_verifySources = true;
    AssetDatabaseStatistics m_statistics;
};

} // namespace illusion
