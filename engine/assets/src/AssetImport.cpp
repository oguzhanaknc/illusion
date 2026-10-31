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

bool readDependency(const VirtualFileSystem& files, const std::string& path, std::vector<std::byte>& out)
{
    const FileError result = files.readFile(path, out);
    if (result == FileError::NotFound && isImportSettingsPath(path))
    {
        out.clear();
        return true;
    }
    return result == FileError::None;
}

bool readImportSettingsFile(const VirtualFileSystem& files, std::string_view assetPath,
                            std::vector<std::byte>& out, std::string* error)
{
    const std::string path = importSettingsPath(assetPath);
    if (const FileError result = files.readFile(path, out); result != FileError::None)
    {
        out.clear();
        if (result != FileError::NotFound)
        {
            if (error != nullptr)
            {
                *error = std::format("{}: {}", path, fileErrorName(result));
            }
            return false;
        }
    }
    return true;
}

std::optional<ImportSettings> parseImportSettingsFile(std::span<const std::byte> sidecar,
                                                      std::string_view assetPath, ImportSettingsKind kind,
                                                      std::string* error)
{
    if (sidecar.empty())
    {
        return ImportSettings{};
    }
    Expected<ImportSettings, ImportSettingsError> settings = parseImportSettings(
        std::string_view(reinterpret_cast<const char*>(sidecar.data()), sidecar.size()), kind);
    if (!settings)
    {
        if (error != nullptr)
        {
            *error = std::format("{}{}{}: {}", assetPath, kImportSettingsSuffix, settings.error().pointer,
                                 settings.error().message);
        }
        return std::nullopt;
    }
    return *settings;
}

std::optional<Image> importImage(std::span<const std::byte> source, std::span<const std::byte> sidecar,
                                 const std::string& path, const ImageImportSettings& settings,
                                 AssetFile& cooked, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::optional<ImportSettings> sidecarSettings =
        parseImportSettingsFile(sidecar, path, ImportSettingsKind::Texture, error);
    if (!sidecarSettings)
    {
        return std::nullopt;
    }
    const ImageImportSettings effective = effectiveImageImport(settings, sidecarSettings->texture);
    std::string decodeError;
    std::optional<Image> image = decodeImage(source, effective.srgb, &decodeError);
    if (!image)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", path, decodeError);
        }
        return std::nullopt;
    }
    if (effective.generateMips)
    {
        generateMips(*image);
    }
    limitImageSize(*image, sidecarSettings->texture.maxSize);
    cooked.type = AssetType::Image;
    cooked.version = kImageFormatVersion;
    cooked.dependencies = {AssetDependency{path, xxhash64(source)},
                           AssetDependency{importSettingsPath(path), xxhash64(sidecar)}};
    cooked.payload = serializeImage(*image);
    return image;
}

std::optional<ModelData> importModel(const VirtualFileSystem& files, std::string_view path, AssetFile& cooked,
                                     std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string normalized = VirtualFileSystem::normalizePath(path).value_or(std::string(path));
    std::vector<std::byte> sidecar;
    if (!readImportSettingsFile(files, normalized, sidecar, error))
    {
        return std::nullopt;
    }
    const std::optional<ImportSettings> settings =
        parseImportSettingsFile(sidecar, normalized, ImportSettingsKind::Model, error);
    if (!settings)
    {
        return std::nullopt;
    }
    std::vector<std::string> sources;
    std::optional<ModelData> model = loadGltf(files, path, error, &sources);
    if (!model)
    {
        return std::nullopt;
    }
    scaleModel(*model, settings->model.scale);
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
    if (!cooked.dependencies.empty())
    {
        cooked.dependencies.push_back(AssetDependency{importSettingsPath(normalized), xxhash64(sidecar)});
    }
    cooked.payload = serializeModel(*model);
    return model;
}

} // namespace illusion::detail
