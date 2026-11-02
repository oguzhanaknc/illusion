#include <illusion/assets/Image.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/ThumbnailCache.hpp>
#include <illusion/editor/core/ThumbnailHost.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstring>
#include <format>
#include <unordered_set>
#include <vector>

namespace illusion::editor
{
namespace
{

// Dependencies deeper than this are not followed (the catalog's dependencies are one or two levels deep).
constexpr u32 kMaxKeyDepth = 8;

u64 hashText(std::string_view text) noexcept
{
    return xxhash64(std::as_bytes(std::span<const char>(text.data(), text.size())));
}

// Appends the hashes of @p path and its dependencies to @p values, each asset once.
void collectKey(const AssetCatalog& catalog, std::string_view path, u32 depth,
                std::unordered_set<std::string_view>& visited, std::vector<u64>& values)
{
    if (!visited.insert(path).second)
    {
        return;
    }
    values.push_back(hashText(path));
    const AssetRecord* record = catalog.find(path);
    if (record == nullptr)
    {
        values.push_back(0); // missing: appearing later changes the key
        return;
    }
    values.push_back(record->contentHash);
    values.push_back(record->importSettingsHash);
    if (depth >= kMaxKeyDepth)
    {
        return;
    }
    for (const std::string& dependency : record->dependencies)
    {
        collectKey(catalog, dependency, depth + 1, visited, values);
    }
}

} // namespace

std::optional<ThumbnailKind> thumbnailKindOf(std::string_view path)
{
    const std::string extension = lowerExtension(path);
    if (extension == ".gltf" || extension == ".glb")
    {
        return ThumbnailKind::Model;
    }
    if (extension == ".material")
    {
        return ThumbnailKind::Material;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
        extension == ".bmp")
    {
        return ThumbnailKind::Texture;
    }
    if (extension == ".hdr")
    {
        return ThumbnailKind::Environment;
    }
    return std::nullopt;
}

std::string_view thumbnailKindName(ThumbnailKind kind) noexcept
{
    switch (kind)
    {
    case ThumbnailKind::Model:
        return "Model";
    case ThumbnailKind::Material:
        return "Material";
    case ThumbnailKind::Texture:
        return "Texture";
    case ThumbnailKind::Environment:
        return "Environment";
    }
    return "Model";
}

u64 thumbnailKey(const AssetCatalog& catalog, std::string_view path)
{
    ILL_PROFILE_SCOPE();
    const AssetRecord* record = catalog.find(path);
    if (record == nullptr || record->kind == "Folder")
    {
        return 0;
    }
    std::vector<u64> values{kThumbnailVersion, kThumbnailSize, hashText(lowerExtension(path))};
    std::unordered_set<std::string_view> visited;
    collectKey(catalog, path, 0, visited, values);
    const u64 key = xxhash64(std::as_bytes(std::span<const u64>(values)));
    return key != 0 ? key : 1;
}

std::string thumbnailPath(u64 key)
{
    return std::format("{}:/{:016x}.png", kThumbnailMount, key);
}

void ThumbnailCache::open(VirtualFileSystem& files) noexcept
{
    m_files = &files;
    m_known.clear();
    m_statistics = {};
}

void ThumbnailCache::close() noexcept
{
    m_files = nullptr;
    m_known.clear();
}

bool ThumbnailCache::find(std::string_view asset, u64 key)
{
    if (m_files == nullptr || key == 0)
    {
        return false;
    }
    FileStat stat;
    if (m_files->stat(thumbnailPath(key), stat) != FileError::None || stat.isDirectory || stat.size == 0)
    {
        return false;
    }
    const auto known = m_known.find(asset);
    if (known == m_known.end())
    {
        m_known.emplace(std::string(asset), key);
    }
    else
    {
        known->second = key;
    }
    return true;
}

Expected<std::string, CommandError> ThumbnailCache::store(std::string_view asset, u64 key,
                                                          const ThumbnailImage& image)
{
    ILL_PROFILE_SCOPE();
    if (m_files == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    const usize bytes = static_cast<usize>(image.size.x) * image.size.y * 4;
    if (image.size.x == 0 || image.size.y == 0 || image.rgba.size() != bytes)
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "the thumbnail image is malformed"));
    }
    Image png;
    png.format = ImageFormat::RGBA8Srgb;
    png.data.resize(bytes);
    std::memcpy(png.data.data(), image.rgba.data(), bytes);
    png.mips.push_back(ImageMip{image.size.x, image.size.y, 0, bytes});
    const std::vector<std::byte> encoded = encodePng(png);
    if (encoded.empty())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "the thumbnail could not be encoded"));
    }
    std::string path = thumbnailPath(key);
    if (const FileError error = m_files->writeFile(path, encoded); error != FileError::None)
    {
        return unexpected(CommandError::make(
            ErrorKind::IoError, "cannot write '" + path + "' (" + std::string(fileErrorName(error)) + ")"));
    }
    const auto known = m_known.find(asset);
    if (known != m_known.end() && known->second != key)
    {
        (void)m_files->remove(thumbnailPath(known->second));
    }
    m_known.insert_or_assign(std::string(asset), key);
    ++m_statistics.stored;
    return path;
}

u32 ThumbnailCache::invalidate(const AssetCatalogChanges& changes, const AssetCatalog& catalog)
{
    if (m_files == nullptr || m_known.empty())
    {
        return 0;
    }
    ILL_PROFILE_SCOPE();
    const u32 before = m_statistics.invalidated;
    std::vector<std::string> changed = changes.modified;
    changed.insert(changed.end(), changes.removed.begin(), changes.removed.end());
    for (const AssetRename& rename : changes.renamed)
    {
        changed.push_back(rename.from);
    }
    for (const std::string& path : changed)
    {
        forget(path);
        for (const std::string& referrer : catalog.referrers(path))
        {
            forget(referrer);
        }
    }
    return m_statistics.invalidated - before;
}

void ThumbnailCache::forget(const std::string& asset)
{
    const auto known = m_known.find(asset);
    if (known == m_known.end())
    {
        return;
    }
    if (m_files->remove(thumbnailPath(known->second)) == FileError::None)
    {
        ++m_statistics.invalidated;
    }
    m_known.erase(known);
}

} // namespace illusion::editor
