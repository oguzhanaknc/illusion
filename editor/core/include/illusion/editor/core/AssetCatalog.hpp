#pragma once
/// @file
/// The asset catalog: every file and folder under `assets:/` with its kind, size, modification stamp,
/// content hash and the assets it depends on, kept up to date incrementally from directory listings or
/// FileWatcher changes. Import settings sidecars (`<asset>.import.json`) are not assets of their own: the
/// catalog records their hash on the asset they belong to.

#include <illusion/core/Base.hpp>

#include <array>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class VirtualFileSystem;
struct FileEvent;
} // namespace illusion

namespace illusion::editor
{

/// The asset kinds, in the order the asset commands list them.
inline constexpr std::array<std::string_view, 8> kAssetKinds{"Folder",  "Scene",       "Material",  "Model",
                                                             "Texture", "Environment", "AudioClip", "Other"};

/// The kind of the asset at @p path (one of kAssetKinds), decided by its extension in any case.
[[nodiscard]] std::string_view assetKindOf(std::string_view path, bool isDirectory);

/// What the catalog knows about one file or folder.
struct AssetRecord
{
    std::string path;        ///< Virtual path, e.g. "assets:/models/ship.glb".
    std::string_view kind{}; ///< One of kAssetKinds.
    u64 size = 0;            ///< Size in bytes (0 for folders).
    i64 modified = 0;        ///< Modification stamp (FileStat::modified; 0 for folders).
    u64 contentHash = 0;     ///< xxhash64() of the file's bytes (0 for folders).
    /// Assets this one refers to, without `#` fragments, sorted and unique: the references of scenes and
    /// materials, and the external buffers and images of `.gltf` files.
    std::vector<std::string> dependencies{};
    std::string error{};        ///< Why the file or its dependencies could not be read ("" if they could).
    u64 importSettingsHash = 0; ///< xxhash64() of its import settings sidecar (0 if it has none).
};

/// An asset that disappeared from one path and appeared at another with the same contents.
struct AssetRename
{
    std::string from; ///< Old path.
    std::string to;   ///< New path.

    /// Renames compare by both paths.
    friend bool operator==(const AssetRename&, const AssetRename&) = default;
};

/// What one AssetCatalog update changed; every list is sorted.
struct AssetCatalogChanges
{
    std::vector<std::string> added{};    ///< Files and folders that appeared.
    std::vector<std::string> modified{}; ///< Files whose contents or import settings sidecar changed.
    std::vector<std::string> removed{};  ///< Files and folders that disappeared.
    /// Files that moved: a removal and an addition of a non-empty file with the same size and content hash,
    /// which are then in neither list above. Made by tools outside the editor; `asset.move` updates
    /// references itself.
    std::vector<AssetRename> renamed{};

    /// True if nothing changed.
    [[nodiscard]] bool empty() const noexcept
    {
        return added.empty() && modified.empty() && removed.empty() && renamed.empty();
    }

    /// Adds the changes of a later update @p next, so that this describes both: a path added and then
    /// removed is in no list, one removed and added again is modified, and so on.
    void merge(const AssetCatalogChanges& next);
};

/// A dependency on an asset that is not in the catalog.
struct MissingDependency
{
    std::string source; ///< The file that refers to it.
    std::string asset;  ///< The missing asset.

    /// Missing dependencies compare by both paths.
    friend bool operator==(const MissingDependency&, const MissingDependency&) = default;
};

/// Every file and folder under `assets:/`, with hashes, dependencies and the reverse references.
///
/// refresh() lists the mount and reads only the files that are new or whose size or modification stamp
/// changed; apply() updates exactly the paths a FileWatcher reported (an Overflow makes it refresh()), and
/// reads every reported file again, so an edit within the file system's stamp resolution is not missed.
/// A sidecar that appears, changes or disappears reports its asset as modified (if the asset exists).
class AssetCatalog
{
public:
    /// Brings the catalog up to date with `assets:/` on @p files and returns what changed. The first call
    /// reports every file as added.
    AssetCatalogChanges refresh(const VirtualFileSystem& files);

    /// Applies @p changes reported by a FileWatcher on the directory of the `assets:` mount (paths
    /// relative to it) and returns what changed. Before the first refresh() it refreshes instead.
    AssetCatalogChanges apply(const VirtualFileSystem& files, std::span<const FileEvent> changes);

    /// Forgets everything (a project was opened or closed).
    void clear() noexcept;

    /// True once refresh() has run since construction or clear().
    [[nodiscard]] bool scanned() const noexcept { return m_scanned; }

    /// The record of @p path, or nullptr if the catalog does not have it.
    [[nodiscard]] const AssetRecord* find(std::string_view path) const;

    /// Every record, by path.
    [[nodiscard]] const std::map<std::string, AssetRecord, std::less<>>& records() const noexcept
    {
        return m_records;
    }

    /// Number of records.
    [[nodiscard]] usize size() const noexcept { return m_records.size(); }

    /// The files whose dependencies refer to @p path (an asset, a sub-asset or a folder and everything in
    /// it; see refersTo()), sorted.
    [[nodiscard]] std::vector<std::string> referrers(std::string_view path) const;

    /// The dependencies on `assets:/` paths that are not in the catalog, sorted by source and asset.
    [[nodiscard]] std::vector<MissingDependency> missing() const;

private:
    // Reads @p path and updates its record; reports it as added or (if its hash changed) modified.
    void scanFile(const VirtualFileSystem& files, const std::string& path, u64 size, i64 modified,
                  AssetCatalogChanges& changes);
    void removeTree(const std::string& path, AssetCatalogChanges& changes);
    // Reads the sidecar @p path (unless @p force is false and its size and stamp are known) and updates the
    // hash on its asset.
    void scanSidecar(const VirtualFileSystem& files, const std::string& path, u64 size, i64 modified,
                     bool force, AssetCatalogChanges& changes);
    void removeSidecar(const std::string& path, AssetCatalogChanges& changes);
    // Sets the sidecar hash of the asset @p owner; reports the asset as modified if it changed.
    void setImportSettingsHash(std::string_view owner, u64 hash, AssetCatalogChanges& changes);
    // Called before a record is erased: remembers a file's size and hash for rename detection.
    void forget(const AssetRecord& record);
    // Sorts @p changes, turns removals and additions of one path into modifications and finds renames.
    void finish(AssetCatalogChanges& changes);

    struct RemovedFile
    {
        u64 size = 0;
        u64 hash = 0;
    };
    struct Sidecar
    {
        u64 size = 0;
        i64 modified = 0;
        u64 hash = 0;
    };

    std::map<std::string, AssetRecord, std::less<>> m_records;
    // Dependency -> files that have it, rebuilt on demand after a change.
    mutable std::map<std::string, std::vector<std::string>, std::less<>> m_referrers;
    mutable bool m_referrersStale = true;
    std::map<std::string, RemovedFile, std::less<>> m_removedHashes; // Erased in the current update.
    std::map<std::string, Sidecar, std::less<>> m_sidecars; // By sidecar path, with or without asset.
    bool m_scanned = false;
};

} // namespace illusion::editor
