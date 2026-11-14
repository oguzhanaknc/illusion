// illusion_pack: builds .illpak archives.
//
//   illusion_pack <directory> <archive.illpak>
//       Packs every file under <directory>.
//   illusion_pack --files <directory> <list.txt> <archive.illpak>
//       Packs the files of <directory> that <list.txt> names: paths relative to <directory> with '/'
//       separators, one per line, in UTF-8 (the editor's export leaves files out this way).
//   illusion_pack --cook <mount> <directory> <archive.illpak>
//       Imports every image, glTF model and material's textures under <directory>, mounted as <mount> (the
//       name the game uses, e.g. "game"), and packs the resulting import cache. Mount the archive as the
//       game's cache mount (with source verification off) to load the cooked assets without shipping the
//       sources.
//   illusion_pack --cook <mount> <directory> <list.txt> <archive.illpak>
//       The same for only the files of <directory> that <list.txt> names (as for --files): what is not listed
//       is neither cooked nor visible to the imports.

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetReflection.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
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
                         "       illusion_pack --files <directory> <list.txt> <archive.illpak>\n"
                         "       illusion_pack --cook <mount> <directory> [<list.txt>] <archive.illpak>\n");
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

std::string text(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

bool readBytes(const std::filesystem::path& path, std::vector<std::byte>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }
    out.resize(static_cast<usize>(file.tellg()));
    file.seekg(0);
    return out.empty() || static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()),
                                                      static_cast<std::streamsize>(out.size())));
}

// The relative paths of a file list, one per line.
bool readList(const std::filesystem::path& list, std::vector<std::string>& out)
{
    std::ifstream in(list, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "illusion_pack: cannot read the list %s\n", text(list).c_str());
        return false;
    }
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            out.push_back(std::move(line));
        }
    }
    return true;
}

int packList(const std::filesystem::path& directory, const std::filesystem::path& list,
             const std::filesystem::path& archive)
{
    std::vector<std::string> names;
    if (!readList(list, names))
    {
        return 1;
    }
    std::vector<ArchiveInput> files;
    for (const std::string& name : names)
    {
        ArchiveInput input{.path = name, .data = {}};
        if (!readBytes(directory / utf8Path(name), input.data))
        {
            std::fprintf(stderr, "illusion_pack: cannot read %s\n", name.c_str());
            return 1;
        }
        files.push_back(std::move(input));
    }
    if (const FileError error = writeArchive(archive, files); error != FileError::None)
    {
        std::fprintf(stderr, "illusion_pack: cannot write %s: %s\n", text(archive).c_str(),
                     std::string(fileErrorName(error)).c_str());
        return 1;
    }
    return 0;
}

int cook(std::string_view mount, std::filesystem::path directory, const std::filesystem::path& archive,
         const std::filesystem::path& list = {})
{
    std::error_code code;
    const std::filesystem::path work = std::filesystem::temp_directory_path(code) /
                                       ("illusion-cook-" + std::to_string(std::random_device{}()));
    const std::filesystem::path cache = work / "cache";
    std::filesystem::create_directories(cache, code);
    struct Cleanup
    {
        const std::filesystem::path& path;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{work};
    if (!list.empty())
    {
        // The cook sees only the listed files: a copy of them under the same relative paths.
        std::vector<std::string> names;
        if (!readList(list, names))
        {
            return 1;
        }
        const std::filesystem::path source = work / "source";
        for (const std::string& name : names)
        {
            const std::filesystem::path to = source / utf8Path(name);
            std::filesystem::create_directories(to.parent_path(), code);
            if (!std::filesystem::copy_file(directory / utf8Path(name), to, code))
            {
                std::fprintf(stderr, "illusion_pack: cannot read %s\n", name.c_str());
                return 1;
            }
        }
        std::filesystem::create_directories(source, code);
        directory = source;
    }
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
    return result.errors.empty() ? pack(cache, archive) : 1;
}

int run(const std::vector<std::string>& arguments)
{
    Logger::instance().setLevel(LogLevel::Warn);
    if (arguments.size() == 2)
    {
        return pack(utf8Path(arguments[0]), utf8Path(arguments[1]));
    }
    if (arguments.size() == 4 && arguments[0] == "--files")
    {
        return packList(utf8Path(arguments[1]), utf8Path(arguments[2]), utf8Path(arguments[3]));
    }
    if (arguments.size() == 4 && arguments[0] == "--cook")
    {
        return cook(arguments[1], utf8Path(arguments[2]), utf8Path(arguments[3]));
    }
    if (arguments.size() == 5 && arguments[0] == "--cook")
    {
        return cook(arguments[1], utf8Path(arguments[2]), utf8Path(arguments[4]), utf8Path(arguments[3]));
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
