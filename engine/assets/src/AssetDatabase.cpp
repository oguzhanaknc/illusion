#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <format>
#include <utility>

namespace illusion
{
namespace
{

// Bump an importer's version when its output changes for the same input, to invalidate old caches.
constexpr std::string_view kImageImporter = "image:1";
constexpr std::string_view kModelImporter = "gltf:1";

void setError(std::string* error, std::string message)
{
    if (error != nullptr)
    {
        *error = std::move(message);
    }
}

} // namespace

AssetDatabase::AssetDatabase(VirtualFileSystem& files, std::string cacheMount)
    : m_files(&files)
    , m_cacheMount(std::move(cacheMount))
{
}

std::string AssetDatabase::cachePath(std::string_view virtualPath, std::string_view importKey) const
{
    const std::string normalized =
        VirtualFileSystem::normalizePath(virtualPath).value_or(std::string(virtualPath));
    const u64 key = fnv1a64(std::format("{}|{}", importKey, normalized));
    return std::format("{}:/imported/{:016x}.illasset", m_cacheMount, key);
}

bool AssetDatabase::hashDependencies(const std::vector<std::string>& paths,
                                     std::vector<AssetDependency>& out) const
{
    out.clear();
    std::vector<std::byte> bytes;
    for (const std::string& path : paths)
    {
        if (m_files->readFile(path, bytes) != FileError::None)
        {
            return false;
        }
        out.push_back(AssetDependency{path, xxhash64(bytes)});
    }
    return true;
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
    std::optional<AssetFile> file = readAssetFile(bytes);
    if (!file || file->type != type || file->version != version || file->dependencies.empty())
    {
        return std::nullopt;
    }
    // Valid only while every source file still has the content it was imported from.
    std::vector<std::byte> source;
    for (const AssetDependency& dependency : file->dependencies)
    {
        if (m_files->readFile(dependency.path, source) != FileError::None ||
            xxhash64(source) != dependency.contentHash)
        {
            return std::nullopt;
        }
    }
    return file;
}

void AssetDatabase::writeCache(const std::string& path, AssetFile file)
{
    if (m_cacheMount.empty())
    {
        return;
    }
    const std::vector<std::byte> bytes = writeAssetFile(file);
    if (const FileError result = m_files->writeFile(path, bytes); result != FileError::None)
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
    const std::string key = std::format("{}|srgb={}|mips={}", kImageImporter, settings.srgb ? 1 : 0,
                                        settings.generateMips ? 1 : 0);
    const std::string cached = cachePath(virtualPath, key);
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
    std::vector<std::byte> bytes;
    if (!path)
    {
        setError(error, std::format("{}: invalid path", virtualPath));
        return std::nullopt;
    }
    if (const FileError result = m_files->readFile(*path, bytes); result != FileError::None)
    {
        setError(error, std::format("{}: {}", *path, fileErrorName(result)));
        return std::nullopt;
    }
    std::string decodeError;
    std::optional<Image> image = decodeImage(bytes, settings.srgb, &decodeError);
    if (!image)
    {
        setError(error, std::format("{}: {}", *path, decodeError));
        return std::nullopt;
    }
    if (settings.generateMips)
    {
        generateMips(*image);
    }
    AssetFile file;
    file.type = AssetType::Image;
    file.version = kImageFormatVersion;
    file.dependencies.push_back(AssetDependency{*path, xxhash64(bytes)});
    file.payload = serializeImage(*image);
    writeCache(cached, std::move(file));
    return image;
}

std::optional<ModelData> AssetDatabase::loadModel(std::string_view virtualPath, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, kModelImporter);
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Model, kModelFormatVersion))
    {
        if (std::optional<ModelData> model = deserializeModel(file->payload))
        {
            ++m_statistics.cacheHits;
            return model;
        }
    }

    ++m_statistics.imports;
    std::vector<std::string> sources;
    std::optional<ModelData> model = loadGltf(*m_files, virtualPath, error, &sources);
    if (!model)
    {
        return std::nullopt;
    }
    AssetFile file;
    file.type = AssetType::Model;
    file.version = kModelFormatVersion;
    if (hashDependencies(sources, file.dependencies))
    {
        file.payload = serializeModel(*model);
        writeCache(cached, std::move(file));
    }
    return model;
}

} // namespace illusion
