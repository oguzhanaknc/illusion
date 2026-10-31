#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/FileWatcher.hpp>

#include <algorithm>
#include <iterator>
#include <optional>

namespace illusion::editor
{
namespace
{

// @p reference without its `#` fragment.
std::string_view withoutFragment(std::string_view reference) noexcept
{
    return reference.substr(0, reference.find('#'));
}

int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// A glTF URI with its percent escapes decoded, or std::nullopt for a malformed escape.
std::optional<std::string> decodeUri(std::string_view uri)
{
    std::string out;
    out.reserve(uri.size());
    for (usize i = 0; i < uri.size(); ++i)
    {
        if (uri[i] != '%')
        {
            out += uri[i];
            continue;
        }
        if (i + 2 >= uri.size())
        {
            return std::nullopt;
        }
        const int high = hexDigit(uri[i + 1]);
        const int low = hexDigit(uri[i + 2]);
        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }
        out += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return out;
}

// Appends the external buffers and images of the glTF file @p json at @p path to @p out.
bool gltfDependencies(std::string_view json, std::string_view path, std::vector<std::string>& out,
                      std::string& error)
{
    Expected<JsonDocument, JsonError> document = JsonDocument::parse(json);
    if (!document)
    {
        error = "not valid JSON (" + document.error().message + ")";
        return false;
    }
    const std::string_view directory = path.substr(0, path.rfind('/') + 1);
    for (const std::string_view list : {std::string_view("buffers"), std::string_view("images")})
    {
        for (const JsonValue item : document->root().find(list).elements())
        {
            const std::optional<std::string_view> uri = item.find("uri").asString();
            if (!uri || uri->starts_with("data:") || uri->find(':') != std::string_view::npos)
            {
                continue; // Embedded, or absolute (not a file of the project).
            }
            const std::optional<std::string> decoded = decodeUri(*uri);
            if (!decoded)
            {
                continue;
            }
            if (std::optional<std::string> normalized =
                    VirtualFileSystem::normalizePath(std::string(directory) + *decoded))
            {
                out.push_back(std::move(*normalized));
            }
        }
    }
    return true;
}

// The dependencies of the file @p path with contents @p text; false with @p error if they are unknown.
bool dependenciesOf(std::string_view text, const std::string& path, std::vector<std::string>& out,
                    std::string& error)
{
    const std::string extension = lowerExtension(path);
    std::vector<AssetReference> references;
    Expected<void, std::string> scanned;
    if (extension == kSceneExtension)
    {
        scanned = sceneReferences(text, path, references);
    }
    else if (extension == kMaterialAssetExtension)
    {
        scanned = materialReferences(text, path, references);
    }
    else if (extension == ".gltf")
    {
        if (!gltfDependencies(text, path, out, error))
        {
            return false;
        }
    }
    if (!scanned)
    {
        error = std::move(scanned).error();
        return false;
    }
    for (const AssetReference& reference : references)
    {
        out.emplace_back(withoutFragment(reference.asset));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

bool parsesDependencies(std::string_view path)
{
    const std::string extension = lowerExtension(path);
    return extension == kSceneExtension || extension == kMaterialAssetExtension || extension == ".gltf";
}

void sortUnique(std::vector<std::string>& paths)
{
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
}

bool contains(const std::vector<std::string>& sorted, std::string_view path)
{
    return std::binary_search(sorted.begin(), sorted.end(), path, std::less<>());
}

void insertSorted(std::vector<std::string>& sorted, const std::string& path)
{
    const auto at = std::lower_bound(sorted.begin(), sorted.end(), path);
    if (at == sorted.end() || *at != path)
    {
        sorted.insert(at, path);
    }
}

void eraseSorted(std::vector<std::string>& sorted, std::string_view path)
{
    const auto at = std::lower_bound(sorted.begin(), sorted.end(), path, std::less<>());
    if (at != sorted.end() && *at == path)
    {
        sorted.erase(at);
    }
}

} // namespace

void AssetCatalogChanges::merge(const AssetCatalogChanges& next)
{
    for (const std::string& path : next.removed)
    {
        eraseSorted(modified, path);
        if (contains(added, path))
        {
            eraseSorted(added, path);
        }
        else
        {
            insertSorted(removed, path);
        }
    }
    for (const std::string& path : next.added)
    {
        if (contains(removed, path))
        {
            eraseSorted(removed, path);
            insertSorted(modified, path);
        }
        else
        {
            insertSorted(added, path);
        }
    }
    for (const std::string& path : next.modified)
    {
        if (!contains(added, path))
        {
            insertSorted(modified, path);
        }
    }
    renamed.insert(renamed.end(), next.renamed.begin(), next.renamed.end());
    std::sort(renamed.begin(), renamed.end(),
              [](const AssetRename& a, const AssetRename& b) { return a.from < b.from; });
}

std::string_view assetKindOf(std::string_view path, bool isDirectory)
{
    if (isDirectory)
    {
        return "Folder";
    }
    const std::string extension = lowerExtension(path);
    if (extension == kSceneExtension)
    {
        return "Scene";
    }
    if (extension == kMaterialAssetExtension)
    {
        return "Material";
    }
    if (extension == ".gltf" || extension == ".glb")
    {
        return "Model";
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
    {
        return "Texture";
    }
    if (extension == ".hdr")
    {
        return "Environment";
    }
    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" || extension == ".ogg")
    {
        return "AudioClip";
    }
    return "Other";
}

AssetCatalogChanges AssetCatalog::refresh(const VirtualFileSystem& files)
{
    ILL_PROFILE_SCOPE();
    AssetCatalogChanges changes;
    std::vector<DirectoryEntry> entries;
    if (files.listRecursive(kAssetsRoot, entries) != FileError::None)
    {
        entries.clear(); // No project: an empty catalog.
    }
    std::vector<std::string> listedPaths;
    listedPaths.reserve(entries.size());
    for (const DirectoryEntry& entry : entries)
    {
        listedPaths.push_back(std::string(kAssetsRoot) + entry.name);
    }
    // The listing is sorted by name, so the paths are too.
    for (auto it = m_sidecars.begin(); it != m_sidecars.end();)
    {
        if (std::binary_search(listedPaths.begin(), listedPaths.end(), it->first))
        {
            ++it;
            continue;
        }
        const std::string owner(importSettingsOwner(it->first));
        it = m_sidecars.erase(it);
        setImportSettingsHash(owner, 0, changes);
    }
    for (auto it = m_records.begin(); it != m_records.end();)
    {
        if (std::binary_search(listedPaths.begin(), listedPaths.end(), it->first))
        {
            ++it;
            continue;
        }
        forget(it->second);
        changes.removed.push_back(it->first);
        it = m_records.erase(it);
    }
    std::string path;
    for (const DirectoryEntry& listed : entries)
    {
        path.assign(kAssetsRoot);
        path += listed.name;
        if (listed.isDirectory)
        {
            const auto known = m_records.find(path);
            if (known != m_records.end() && known->second.kind != "Folder")
            {
                removeTree(path, changes); // A file replaced by a folder.
            }
            if (m_records.emplace(path, AssetRecord{.path = path, .kind = "Folder"}).second)
            {
                changes.added.push_back(path);
            }
            continue;
        }
        if (isImportSettingsPath(path))
        {
            scanSidecar(files, path, listed.size, listed.modified, false, changes);
            continue;
        }
        const auto known = m_records.find(path);
        if (known != m_records.end() && known->second.kind != "Folder" && known->second.size == listed.size &&
            known->second.modified == listed.modified)
        {
            continue;
        }
        scanFile(files, path, listed.size, listed.modified, changes);
    }
    m_scanned = true;
    finish(changes);
    return changes;
}

AssetCatalogChanges AssetCatalog::apply(const VirtualFileSystem& files, std::span<const FileEvent> changes)
{
    ILL_PROFILE_SCOPE();
    const bool overflow = std::any_of(changes.begin(), changes.end(), [](const FileEvent& change) {
        return change.kind == FileEventKind::Overflow;
    });
    if (!m_scanned || overflow)
    {
        return refresh(files);
    }
    AssetCatalogChanges result;
    std::vector<DirectoryEntry> entries;
    std::string path;
    for (const FileEvent& change : changes)
    {
        path.assign(kAssetsRoot);
        path += change.path;
        FileStat stat;
        const FileError error =
            change.kind == FileEventKind::Removed ? FileError::NotFound : files.stat(path, stat);
        if (error != FileError::None)
        {
            if (m_sidecars.find(path) != m_sidecars.end())
            {
                removeSidecar(path, result);
            }
            else
            {
                removeTree(path, result);
            }
            continue;
        }
        if (!stat.isDirectory && isImportSettingsPath(path))
        {
            scanSidecar(files, path, stat.size, stat.modified, true, result);
            continue;
        }
        if (!stat.isDirectory)
        {
            scanFile(files, path, stat.size, stat.modified, result);
            continue;
        }
        if (const auto known = m_records.find(path);
            known != m_records.end() && known->second.kind != "Folder")
        {
            removeTree(path, result); // A file replaced by a folder.
        }
        if (m_records.find(path) == m_records.end())
        {
            m_records.emplace(path, AssetRecord{.path = path, .kind = "Folder"});
            result.added.push_back(path);
        }
        // The files inside a folder that appeared are not always reported one by one.
        if (change.kind == FileEventKind::Created && files.listRecursive(path, entries) == FileError::None)
        {
            const std::string base = path + "/";
            for (const DirectoryEntry& listed : entries)
            {
                const std::string inner = base + listed.name;
                if (listed.isDirectory)
                {
                    if (m_records.emplace(inner, AssetRecord{.path = inner, .kind = "Folder"}).second)
                    {
                        result.added.push_back(inner);
                    }
                }
                else if (isImportSettingsPath(inner))
                {
                    scanSidecar(files, inner, listed.size, listed.modified, true, result);
                }
                else
                {
                    scanFile(files, inner, listed.size, listed.modified, result);
                }
            }
        }
    }
    finish(result);
    return result;
}

void AssetCatalog::clear() noexcept
{
    m_records.clear();
    m_referrers.clear();
    m_referrersStale = true;
    m_removedHashes.clear();
    m_sidecars.clear();
    m_scanned = false;
}

const AssetRecord* AssetCatalog::find(std::string_view path) const
{
    const auto it = m_records.find(path);
    return it != m_records.end() ? &it->second : nullptr;
}

std::vector<std::string> AssetCatalog::referrers(std::string_view path) const
{
    ILL_PROFILE_SCOPE();
    if (m_referrersStale)
    {
        m_referrers.clear();
        for (const auto& [source, record] : m_records)
        {
            for (const std::string& dependency : record.dependencies)
            {
                m_referrers[dependency].push_back(source);
            }
        }
        m_referrersStale = false;
    }
    std::vector<std::string> sources;
    const std::string_view base = withoutFragment(path);
    for (auto it = m_referrers.lower_bound(base); it != m_referrers.end() && it->first.starts_with(base);
         ++it)
    {
        if (refersTo(it->first, base))
        {
            sources.insert(sources.end(), it->second.begin(), it->second.end());
        }
    }
    sortUnique(sources);
    return sources;
}

std::vector<MissingDependency> AssetCatalog::missing() const
{
    std::vector<MissingDependency> out;
    for (const auto& [source, record] : m_records)
    {
        for (const std::string& dependency : record.dependencies)
        {
            if (dependency.starts_with(kAssetsRoot) && m_records.find(dependency) == m_records.end())
            {
                out.push_back({source, dependency});
            }
        }
    }
    return out;
}

void AssetCatalog::scanFile(const VirtualFileSystem& files, const std::string& path, u64 size, i64 modified,
                            AssetCatalogChanges& changes)
{
    auto known = m_records.find(path);
    if (known != m_records.end() && known->second.kind == "Folder")
    {
        removeTree(path, changes); // A folder replaced by a file.
        known = m_records.end();
    }
    AssetRecord record{.path = path, .kind = assetKindOf(path, false), .size = size, .modified = modified};
    if (const auto sidecar = m_sidecars.find(importSettingsPath(path)); sidecar != m_sidecars.end())
    {
        record.importSettingsHash = sidecar->second.hash;
    }
    std::string text;
    if (const FileError error = files.readText(path, text); error != FileError::None)
    {
        record.error = "cannot read it (" + std::string(fileErrorName(error)) + ")";
    }
    else
    {
        record.size = text.size();
        record.contentHash = xxhash64(std::as_bytes(std::span(text.data(), text.size())));
        if (known != m_records.end() && known->second.contentHash == record.contentHash)
        {
            known->second.size = record.size;
            known->second.modified = modified;
            return; // Touched, not changed.
        }
        if (parsesDependencies(path) && !dependenciesOf(text, path, record.dependencies, record.error))
        {
            record.dependencies.clear();
        }
    }
    m_referrersStale = true;
    if (known == m_records.end())
    {
        changes.added.push_back(path);
        m_records.emplace(path, std::move(record));
    }
    else
    {
        changes.modified.push_back(path);
        known->second = std::move(record);
    }
}

void AssetCatalog::removeTree(const std::string& path, AssetCatalogChanges& changes)
{
    if (const auto it = m_records.find(path); it != m_records.end())
    {
        forget(it->second);
        changes.removed.push_back(it->first);
        m_records.erase(it);
    }
    // Look from "<path>/": "<path>.png" sorts between "<path>" and the paths inside it.
    const std::string prefix = path + "/";
    for (auto it = m_records.lower_bound(prefix); it != m_records.end() && it->first.starts_with(prefix);)
    {
        forget(it->second);
        changes.removed.push_back(it->first);
        it = m_records.erase(it);
    }
    // The sidecars in a removed folder went with it, and so did their assets.
    for (auto it = m_sidecars.lower_bound(prefix); it != m_sidecars.end() && it->first.starts_with(prefix);)
    {
        it = m_sidecars.erase(it);
    }
}

void AssetCatalog::scanSidecar(const VirtualFileSystem& files, const std::string& path, u64 size,
                               i64 modified, bool force, AssetCatalogChanges& changes)
{
    const auto known = m_sidecars.find(path);
    if (!force && known != m_sidecars.end() && known->second.size == size &&
        known->second.modified == modified)
    {
        return;
    }
    std::vector<std::byte> bytes;
    if (files.readFile(path, bytes) != FileError::None)
    {
        bytes.clear(); // Unreadable: the import fails with the reason; record that it changed.
    }
    // A sidecar always hashes to something other than 0, the "no sidecar" value.
    const u64 hash = std::max<u64>(xxhash64(bytes), 1);
    m_sidecars[path] = Sidecar{size, modified, hash};
    setImportSettingsHash(importSettingsOwner(path), hash, changes);
}

void AssetCatalog::removeSidecar(const std::string& path, AssetCatalogChanges& changes)
{
    const std::string owner(importSettingsOwner(path));
    m_sidecars.erase(path);
    setImportSettingsHash(owner, 0, changes);
}

void AssetCatalog::setImportSettingsHash(std::string_view owner, u64 hash, AssetCatalogChanges& changes)
{
    const auto record = m_records.find(owner);
    if (record == m_records.end() || record->second.kind == "Folder" ||
        record->second.importSettingsHash == hash)
    {
        return;
    }
    record->second.importSettingsHash = hash;
    changes.modified.push_back(record->first);
}

void AssetCatalog::forget(const AssetRecord& record)
{
    m_referrersStale = true;
    if (record.kind != "Folder" && record.size > 0 && record.error.empty())
    {
        m_removedHashes[record.path] = {record.size, record.contentHash};
    }
}

void AssetCatalog::finish(AssetCatalogChanges& changes)
{
    sortUnique(changes.removed);
    sortUnique(changes.added);
    sortUnique(changes.modified);
    // An asset added in this update whose sidecar also appeared is just added.
    std::erase_if(changes.modified, [&](const std::string& path) { return contains(changes.added, path); });
    // A path removed and added again in one update was replaced: a modification.
    std::vector<std::string> replaced;
    std::set_intersection(changes.removed.begin(), changes.removed.end(), changes.added.begin(),
                          changes.added.end(), std::back_inserter(replaced));
    if (!replaced.empty())
    {
        const auto wasReplaced = [&](const std::string& path) {
            return std::binary_search(replaced.begin(), replaced.end(), path);
        };
        std::erase_if(changes.removed, wasReplaced);
        std::erase_if(changes.added, wasReplaced);
        changes.modified.insert(changes.modified.end(), replaced.begin(), replaced.end());
        sortUnique(changes.modified);
    }
    // Renames: a removed file whose contents appeared at an added path. Removed records are gone, so match
    // by the hashes remembered before removal.
    for (auto removed = changes.removed.begin(); removed != changes.removed.end();)
    {
        const auto previous = m_removedHashes.find(*removed);
        const auto match =
            previous == m_removedHashes.end()
                ? changes.added.end()
                : std::find_if(changes.added.begin(), changes.added.end(), [&](const std::string& added) {
                      const AssetRecord* record = find(added);
                      return record != nullptr && record->kind != "Folder" && record->error.empty() &&
                             record->size == previous->second.size &&
                             record->contentHash == previous->second.hash;
                  });
        if (match == changes.added.end())
        {
            ++removed;
            continue;
        }
        changes.renamed.push_back({*removed, *match});
        changes.added.erase(match);
        removed = changes.removed.erase(removed);
    }
    m_removedHashes.clear();
    std::sort(changes.renamed.begin(), changes.renamed.end(),
              [](const AssetRename& a, const AssetRename& b) { return a.from < b.from; });
}

} // namespace illusion::editor
