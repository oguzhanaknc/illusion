#include "TestSupport.hpp"

#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace illusion;
using test::TempDirectory;

namespace
{

std::vector<std::byte> bytesOf(std::string_view text)
{
    const auto span = std::as_bytes(std::span(text.data(), text.size()));
    return {span.begin(), span.end()};
}

std::vector<ArchiveInput> sampleFiles()
{
    std::vector<ArchiveInput> files;
    files.push_back({"readme.txt", bytesOf("hello archive")});
    files.push_back({"textures/rock.png", bytesOf("not really a png")});
    files.push_back({"textures/sky/day.hdr", bytesOf(std::string(1000, 'x'))});
    files.push_back({"empty.bin", {}});
    files.push_back({"models/a.gltf", bytesOf("{}")});
    files.push_back({"models.txt", bytesOf("sorts between models/ entries")});
    return files;
}

std::vector<std::byte> readAll(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::vector<std::byte> bytes(static_cast<usize>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

void writeAll(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("archives round-trip files and list directories")
{
    TempDirectory dir;
    const std::filesystem::path path = dir.path / "data.illpak";
    REQUIRE(writeArchive(path, sampleFiles()) == FileError::None);

    std::string error;
    const auto reader = ArchiveReader::open(path, &error);
    INFO(error);
    REQUIRE(reader != nullptr);
    CHECK(reader->entries().size() == 6);
    for (const ArchiveInput& input : sampleFiles())
    {
        const ArchiveEntry* entry = reader->find(input.path);
        REQUIRE(entry != nullptr);
        CHECK(entry->offset % 16 == 0);
        std::vector<std::byte> read;
        CHECK(reader->read(*entry, read) == FileError::None);
        CHECK(read == input.data);
    }
    CHECK(reader->find("textures") == nullptr);
    CHECK(reader->find("missing.txt") == nullptr);
    CHECK(reader->isDirectory(""));
    CHECK(reader->isDirectory("textures/sky"));
    CHECK_FALSE(reader->isDirectory("readme.txt"));
    CHECK_FALSE(reader->isDirectory("text"));

    std::vector<DirectoryEntry> entries;
    REQUIRE(reader->list("", entries) == FileError::None);
    REQUIRE(entries.size() == 5);
    CHECK(entries[0].name == "empty.bin");
    CHECK(entries[1].name == "models");
    CHECK(entries[1].isDirectory);
    CHECK(entries[2].name == "models.txt");
    CHECK(entries[3].name == "readme.txt");
    CHECK(entries[3].size == 13);
    CHECK(entries[4].name == "textures");
    REQUIRE(reader->list("textures", entries) == FileError::None);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "rock.png");
    CHECK(entries[1].name == "sky");
    CHECK(reader->list("readme.txt", entries) == FileError::NotFound);
}

TEST_CASE("archives are deterministic and reject invalid input")
{
    TempDirectory dir;
    std::vector<ArchiveInput> files = sampleFiles();
    REQUIRE(writeArchive(dir.path / "a.illpak", files) == FileError::None);
    std::reverse(files.begin(), files.end());
    REQUIRE(writeArchive(dir.path / "b.illpak", files) == FileError::None);
    CHECK(readAll(dir.path / "a.illpak") == readAll(dir.path / "b.illpak"));

    for (const char* bad : {"../escape.txt", "a//b", "/absolute", "dir/./x", "back\\slash", ""})
    {
        CAPTURE(bad);
        const std::vector<ArchiveInput> invalid = {{bad, {}}};
        CHECK(writeArchive(dir.path / "bad.illpak", invalid) == FileError::InvalidPath);
    }
    const std::vector<ArchiveInput> duplicate = {{"x.txt", {}}, {"x.txt", {}}};
    CHECK(writeArchive(dir.path / "dup.illpak", duplicate) == FileError::InvalidPath);
    CHECK(writeArchive(dir.path / "empty.illpak", {}) == FileError::None);
    REQUIRE(ArchiveReader::open(dir.path / "empty.illpak") != nullptr);
    CHECK(ArchiveReader::open(dir.path / "empty.illpak")->entries().empty());
}

TEST_CASE("damaged archives are detected")
{
    TempDirectory dir;
    const std::filesystem::path path = dir.path / "data.illpak";
    REQUIRE(writeArchive(path, sampleFiles()) == FileError::None);
    const std::vector<std::byte> good = readAll(path);
    std::string error;

    // Damaged file data: the archive opens, but reading that file fails its hash check.
    {
        const auto reader = ArchiveReader::open(path);
        const ArchiveEntry entry = *reader->find("readme.txt");
        std::vector<std::byte> bad = good;
        bad[static_cast<usize>(entry.offset)] ^= std::byte{1};
        writeAll(dir.path / "data-damaged.illpak", bad);
        const auto damaged = ArchiveReader::open(dir.path / "data-damaged.illpak", &error);
        REQUIRE(damaged != nullptr);
        std::vector<std::byte> out;
        CHECK(damaged->read(*damaged->find("readme.txt"), out) == FileError::IoError);
        CHECK(damaged->read(*damaged->find("textures/rock.png"), out) == FileError::None);
    }
    // Damaged table of contents, truncation and foreign files fail to open.
    std::vector<std::byte> badToc = good;
    badToc[good.size() - 3] ^= std::byte{1};
    writeAll(dir.path / "toc.illpak", badToc);
    CHECK(ArchiveReader::open(dir.path / "toc.illpak", &error) == nullptr);
    CHECK(error.find("damaged") != std::string::npos);
    for (usize size : {usize{0}, usize{10}, usize{48}, good.size() - 1})
    {
        writeAll(dir.path / "short.illpak",
                 std::vector<std::byte>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(size)));
        CHECK(ArchiveReader::open(dir.path / "short.illpak") == nullptr);
    }
    writeAll(dir.path / "text.illpak",
             bytesOf("this is not an archive at all, just some text padding to 48+"));
    CHECK(ArchiveReader::open(dir.path / "text.illpak", &error) == nullptr);
    CHECK(error == "not an archive");
    CHECK(ArchiveReader::open(dir.path / "missing.illpak") == nullptr);
}

TEST_CASE("archives mount into the virtual file system read-only")
{
    TempDirectory dir;
    REQUIRE(writeArchive(dir.path / "data.illpak", sampleFiles()) == FileError::None);
    VirtualFileSystem files;
    REQUIRE(files.mountArchive("pak", dir.path / "data.illpak") == FileError::None);
    CHECK(files.isMounted("pak"));

    std::string text;
    REQUIRE(files.readText("pak:/readme.txt", text) == FileError::None);
    CHECK(text == "hello archive");
    std::vector<std::byte> bytes;
    CHECK(files.readFile("pak:/textures/sky/day.hdr", bytes) == FileError::None);
    CHECK(bytes.size() == 1000);
    CHECK(files.readFile("pak:/missing.txt", bytes) == FileError::NotFound);
    CHECK(files.readFile("pak:/textures/../readme.txt", bytes) == FileError::InvalidPath);
    CHECK(files.exists("pak:/textures"));
    CHECK(files.exists("pak:/empty.bin"));
    CHECK_FALSE(files.exists("pak:/nothing"));

    std::vector<DirectoryEntry> entries;
    REQUIRE(files.list("pak:/textures/sky", entries) == FileError::None);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "day.hdr");

    CHECK(files.writeText("pak:/new.txt", "x") == FileError::ReadOnly);
    std::filesystem::path resolved;
    CHECK(files.resolve("pak:/readme.txt", resolved) == FileError::Unsupported);

    CHECK(files.mountArchive("pak2", dir.path / "missing.illpak") == FileError::NotFound);
    writeAll(dir.path / "junk.illpak", bytesOf("junk"));
    CHECK(files.mountArchive("pak2", dir.path / "junk.illpak") == FileError::IoError);
    CHECK(files.mountArchive("Bad Name", dir.path / "data.illpak") == FileError::InvalidPath);
}

TEST_CASE("packDirectory packs a directory tree")
{
    TempDirectory dir;
    const std::filesystem::path source = dir.path / "source";
    std::filesystem::create_directories(source / "a" / "b");
    writeAll(source / "top.txt", bytesOf("top"));
    writeAll(source / "a" / "b" / "deep.txt", bytesOf("deep"));
    REQUIRE(packDirectory(source, dir.path / "out.illpak") == FileError::None);
    CHECK(packDirectory(dir.path / "missing", dir.path / "none.illpak") == FileError::NotFound);

    VirtualFileSystem files;
    REQUIRE(files.mountArchive("pak", dir.path / "out.illpak") == FileError::None);
    std::string text;
    CHECK(files.readText("pak:/a/b/deep.txt", text) == FileError::None);
    CHECK(text == "deep");
    CHECK(files.readText("pak:/top.txt", text) == FileError::None);
    CHECK(text == "top");
}
