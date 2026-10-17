#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
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
        if (extension == ".gltf" || extension == ".glb")
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
