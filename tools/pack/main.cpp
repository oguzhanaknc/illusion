// illusion_pack: builds .illpak archives.
//
//   illusion_pack <directory> <archive.illpak>
//       Packs every file under <directory>.
//   illusion_pack --cook <mount> <directory> <archive.illpak>
//       Imports every image and glTF model under <directory>, mounted as <mount> (the name the game uses,
//       e.g. "game"), and packs the resulting import cache. Mount the archive as the game's cache mount
//       (with source verification off) to load the cooked assets without shipping the sources.

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

int usage()
{
    std::fprintf(stderr, "usage: illusion_pack <directory> <archive.illpak>\n"
                         "       illusion_pack --cook <mount> <directory> <archive.illpak>\n");
    return 2;
}

int pack(const std::filesystem::path& directory, const std::filesystem::path& archive)
{
    if (const FileError error = packDirectory(directory, archive); error != FileError::None)
    {
        std::fprintf(stderr, "illusion_pack: cannot pack %s: %s\n", directory.string().c_str(),
                     std::string(fileErrorName(error)).c_str());
        return 1;
    }
    return 0;
}

int cook(std::string_view mount, const std::filesystem::path& directory, const std::filesystem::path& archive)
{
    std::error_code code;
    const std::filesystem::path cache =
        std::filesystem::temp_directory_path(code) / ("illusion-cook-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(cache, code);
    VirtualFileSystem files;
    if (files.mount(mount, directory) != FileError::None ||
        files.mount("cookcache", cache, MountAccess::ReadWrite) != FileError::None)
    {
        std::fprintf(stderr, "illusion_pack: cannot mount %s\n", directory.string().c_str());
        return 1;
    }
    const CookResult result = cookAssets(files, std::string(mount) + ":/", "cookcache");
    for (const std::string& error : result.errors)
    {
        std::fprintf(stderr, "illusion_pack: %s\n", error.c_str());
    }
    std::printf("cooked %u model(s) and %u image(s)\n", result.models, result.images);
    const int status = result.errors.empty() ? pack(cache, archive) : 1;
    std::filesystem::remove_all(cache, code);
    return status;
}

} // namespace

int main(int argc, char** argv)
{
    Logger::instance().setLevel(LogLevel::Warn);
    if (argc == 3)
    {
        return pack(utf8Path(argv[1]), utf8Path(argv[2]));
    }
    if (argc == 5 && std::string_view(argv[1]) == "--cook")
    {
        return cook(argv[2], utf8Path(argv[3]), utf8Path(argv[4]));
    }
    return usage();
}
