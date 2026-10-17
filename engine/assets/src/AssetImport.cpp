#include "AssetImport.hpp"

#include <illusion/core/Hash.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <format>
#include <utility>

namespace illusion::detail
{

std::string imageImportKey(const ImageImportSettings& settings)
{
    return std::format("{}|srgb={}|mips={}", kImageImporter, settings.srgb ? 1 : 0,
                       settings.generateMips ? 1 : 0);
}

std::string assetCachePath(std::string_view cacheMount, std::string_view virtualPath,
                           std::string_view importKey)
{
    const std::string normalized =
        VirtualFileSystem::normalizePath(virtualPath).value_or(std::string(virtualPath));
    const u64 key = fnv1a64(std::format("{}|{}", importKey, normalized));
    return std::format("{}:/imported/{:016x}.illasset", cacheMount, key);
}

std::optional<AssetFile> parseCache(std::span<const std::byte> bytes, AssetType type, u32 version)
{
    std::optional<AssetFile> file = readAssetFile(bytes);
    if (!file || file->type != type || file->version != version || file->dependencies.empty())
    {
        return std::nullopt;
    }
    return file;
}

bool dependenciesUnchanged(const AssetFile& file, std::span<const std::vector<std::byte>> contents)
{
    if (contents.size() != file.dependencies.size())
    {
        return false;
    }
    for (usize i = 0; i < contents.size(); ++i)
    {
        if (xxhash64(contents[i]) != file.dependencies[i].contentHash)
        {
            return false;
        }
    }
    return true;
}

std::optional<Image> importImage(std::span<const std::byte> source, const std::string& path,
                                 const ImageImportSettings& settings, AssetFile& cooked, std::string* error)
{
    ILL_PROFILE_SCOPE();
    std::string decodeError;
    std::optional<Image> image = decodeImage(source, settings.srgb, &decodeError);
    if (!image)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", path, decodeError);
        }
        return std::nullopt;
    }
    if (settings.generateMips)
    {
        generateMips(*image);
    }
    cooked.type = AssetType::Image;
    cooked.version = kImageFormatVersion;
    cooked.dependencies = {AssetDependency{path, xxhash64(source)}};
    cooked.payload = serializeImage(*image);
    return image;
}

std::optional<ModelData> importModel(const VirtualFileSystem& files, std::string_view path, AssetFile& cooked,
                                     std::string* error)
{
    ILL_PROFILE_SCOPE();
    std::vector<std::string> sources;
    std::optional<ModelData> model = loadGltf(files, path, error, &sources);
    if (!model)
    {
        return std::nullopt;
    }
    cooked.type = AssetType::Model;
    cooked.version = kModelFormatVersion;
    cooked.dependencies.clear();
    cooked.payload.clear();
    std::vector<std::byte> bytes;
    for (const std::string& source : sources)
    {
        if (files.readFile(source, bytes) != FileError::None)
        {
            cooked.dependencies.clear();
            return model; // loaded, but not cacheable
        }
        cooked.dependencies.push_back(AssetDependency{source, xxhash64(bytes)});
    }
    cooked.payload = serializeModel(*model);
    return model;
}

} // namespace illusion::detail
