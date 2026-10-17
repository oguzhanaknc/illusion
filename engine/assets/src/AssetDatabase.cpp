#include "AssetImport.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <format>
#include <utility>

namespace illusion
{

AssetDatabase::AssetDatabase(VirtualFileSystem& files, std::string cacheMount)
    : m_files(&files)
    , m_cacheMount(std::move(cacheMount))
{
}

std::string AssetDatabase::cachePath(std::string_view virtualPath, std::string_view importKey) const
{
    return detail::assetCachePath(m_cacheMount, virtualPath, importKey);
}

std::optional<AssetFile> AssetDatabase::readCache(const std::string& path, AssetType type, u32 version) const
{
    if (m_cacheMount.empty())
    {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    if (m_files->readFile(path, bytes) != FileError::None)
    {
        return std::nullopt;
    }
    std::optional<AssetFile> file = detail::parseCache(bytes, type, version);
    if (!file)
    {
        return std::nullopt;
    }
    // Valid only while every source file still has the content it was imported from.
    std::vector<std::vector<std::byte>> contents(file->dependencies.size());
    for (usize i = 0; i < contents.size(); ++i)
    {
        if (m_files->readFile(file->dependencies[i].path, contents[i]) != FileError::None)
        {
            return std::nullopt;
        }
    }
    if (!detail::dependenciesUnchanged(*file, contents))
    {
        return std::nullopt;
    }
    return file;
}

void AssetDatabase::writeCache(const std::string& path, const AssetFile& file)
{
    if (m_cacheMount.empty() || file.dependencies.empty())
    {
        return;
    }
    if (const FileError result = m_files->writeFile(path, writeAssetFile(file)); result != FileError::None)
    {
        ILL_LOG_WARN("Assets", "cannot write import cache {}: {}", path, fileErrorName(result));
        return;
    }
    ++m_statistics.cacheWrites;
}

std::optional<Image> AssetDatabase::loadImage(std::string_view virtualPath,
                                              const ImageImportSettings& settings, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, detail::imageImportKey(settings));
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Image, kImageFormatVersion))
    {
        if (std::optional<Image> image = deserializeImage(file->payload))
        {
            ++m_statistics.cacheHits;
            return image;
        }
    }

    ++m_statistics.imports;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: invalid path", virtualPath);
        }
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    if (const FileError result = m_files->readFile(*path, bytes); result != FileError::None)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", *path, fileErrorName(result));
        }
        return std::nullopt;
    }
    AssetFile cooked;
    std::optional<Image> image = detail::importImage(bytes, *path, settings, cooked, error);
    if (image)
    {
        writeCache(cached, cooked);
    }
    return image;
}

std::optional<ModelData> AssetDatabase::loadModel(std::string_view virtualPath, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, detail::kModelImporter);
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Model, kModelFormatVersion))
    {
        if (std::optional<ModelData> model = deserializeModel(file->payload))
        {
            ++m_statistics.cacheHits;
            return model;
        }
    }

    ++m_statistics.imports;
    AssetFile cooked;
    std::optional<ModelData> model = detail::importModel(*m_files, virtualPath, cooked, error);
    if (model)
    {
        writeCache(cached, cooked);
    }
    return model;
}

} // namespace illusion
