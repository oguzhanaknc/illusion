#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace illusion
{
namespace
{

std::string lowerExtension(std::string_view name)
{
    const usize dot = name.rfind('.');
    std::string extension(dot == std::string_view::npos ? std::string_view() : name.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return extension;
}

bool isImage(std::string_view extension)
{
    constexpr std::array<std::string_view, 6> kImages = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"};
    return std::find(kImages.begin(), kImages.end(), extension) != kImages.end();
}

// Checks a material file and cooks its textures with the settings the renderer loads them with.
void cookMaterial(VirtualFileSystem& files, AssetDatabase& database, const std::string& path,
                  CookResult& result)
{
    std::vector<std::byte> bytes;
    if (const FileError error = files.readFile(path, bytes); error != FileError::None)
    {
        result.errors.push_back(std::format("{}: {}", path, fileErrorName(error)));
        return;
    }
    const Expected<MaterialAsset, std::string> material =
        parseMaterialAsset(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!material)
    {
        result.errors.push_back(std::format("{}: {}", path, material.error()));
        return;
    }
    ++result.materials;
    for (const MaterialTextureUse& use : materialTextureUses(material->textures))
    {
        std::string error;
        if (use.texture && !database.loadImage(use.texture.path(), {.srgb = use.srgb}, &error))
        {
            result.errors.push_back(std::format("{}: {}", path, error));
        }
    }
}

void cookDirectory(VirtualFileSystem& files, AssetDatabase& database, const std::string& directory,
                   CookResult& result)
{
    std::vector<DirectoryEntry> entries;
    if (const FileError error = files.list(directory, entries); error != FileError::None)
    {
        result.errors.push_back(std::format("{}: {}", directory, fileErrorName(error)));
        return;
    }
    for (const DirectoryEntry& entry : entries)
    {
        const std::string path =
            directory.ends_with('/') ? directory + entry.name : directory + "/" + entry.name;
        if (entry.isDirectory)
        {
            cookDirectory(files, database, path, result);
            continue;
        }
        const std::string extension = lowerExtension(entry.name);
        std::string error;
        if (isImportSettingsPath(path))
        {
            // Read with the asset they belong to; one without such an asset is a mistake worth reporting.
            const std::string_view owner = importSettingsOwner(path);
            if (importSettingsKindOf(owner) == ImportSettingsKind::None)
            {
                result.errors.push_back(
                    std::format("{}: {} files have no import settings", path, lowerExtension(owner)));
            }
            else if (!files.exists(owner))
            {
                result.errors.push_back(std::format("{}: the asset {} does not exist", path, owner));
            }
        }
        else if (extension == ".gltf" || extension == ".glb")
        {
            if (database.loadModel(path, &error))
            {
                ++result.models;
            }
            else
            {
                result.errors.push_back(error);
            }
        }
        else if (extension == kMaterialAssetExtension)
        {
            cookMaterial(files, database, path, result);
        }
        else if (isImage(extension))
        {
            if (database.loadImage(path, {}, &error))
            {
                ++result.images;
            }
            else
            {
                result.errors.push_back(error);
            }
        }
    }
}

} // namespace

CookResult cookAssets(VirtualFileSystem& files, std::string_view directory, std::string_view cacheMount)
{
    ILL_PROFILE_SCOPE();
    CookResult result;
    AssetDatabase database(files, std::string(cacheMount));
    cookDirectory(files, database, std::string(directory), result);
    return result;
}

} // namespace illusion
