// illusion_pack: builds .illpak archives.
//
//   illusion_pack <directory> <archive.illpak>
//       Packs every file under <directory>.
//   illusion_pack --cook <mount> <directory> <archive.illpak>
//       Imports every image, glTF model and material's textures under <directory>, mounted as <mount> (the
//       name the game uses, e.g. "game"), and packs the resulting import cache. Mount the archive as the
//       game's cache mount (with source verification off) to load the cooked assets without shipping the
//       sources.

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetReflection.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

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
    const std::filesystem::path cache = std::filesystem::temp_directory_path(code) /
                                        ("illusion-cook-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(cache, code);
    reflectAssetStructs(); // material files are read through reflection
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
    std::printf("cooked %u model(s), %u image(s) and %u material(s)\n", result.models, result.images,
                result.materials);
    const int status = result.errors.empty() ? pack(cache, archive) : 1;
    std::filesystem::remove_all(cache, code);
    return status;
}

int run(const std::vector<std::string>& arguments)
{
    Logger::instance().setLevel(LogLevel::Warn);
    if (arguments.size() == 2)
    {
        return pack(utf8Path(arguments[0]), utf8Path(arguments[1]));
    }
    if (arguments.size() == 4 && arguments[0] == "--cook")
    {
        return cook(arguments[1], utf8Path(arguments[2]), utf8Path(arguments[3]));
    }
    return usage();
}

} // namespace

#if defined(_WIN32)
// Windows passes narrow arguments in the ANSI code page, which is not UTF-8 and cannot hold every path (a
// non-ASCII directory made utf8Path() fail); take the UTF-16 arguments and convert them to UTF-8.
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
    {
        const std::u8string text = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return run(arguments);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
