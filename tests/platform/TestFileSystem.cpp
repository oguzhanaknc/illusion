#include "TestSupport.hpp"

#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace illusion;

using test::TempDirectory;

TEST_CASE("mount validates names and roots")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    CHECK(vfs.mount("game", dir.path) == FileError::None);
    CHECK(vfs.isMounted("game"));
    CHECK(vfs.mount("Game", dir.path) == FileError::InvalidPath);
    CHECK(vfs.mount("", dir.path) == FileError::InvalidPath);
    CHECK(vfs.mount("bad name", dir.path) == FileError::InvalidPath);
    CHECK(vfs.mount("missing", dir.path / "does-not-exist") == FileError::NotFound);
    CHECK(vfs.unmount("game"));
    CHECK_FALSE(vfs.unmount("game"));
    CHECK_FALSE(vfs.isMounted("game"));
}

TEST_CASE("virtual paths cannot escape their mount")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("game", dir.path) == FileError::None);
    std::filesystem::path out;
    CHECK(vfs.resolve("game:/a/b.txt", out) == FileError::None);
    CHECK(out == dir.path.lexically_normal() / "a" / "b.txt");
    CHECK(vfs.resolve("game:/", out) == FileError::None);

    for (const char* bad :
         {"game:/../secret.txt", "game:/a/../../x", "game:/./a", "game:/a//b", "game:/a/", "game:/a\\b",
          "game:/C:/x", "game", "game:a", "/etc/passwd", ":/x", "game:/nul", "game:/CON.txt",
          "game:/saves/com1.dat", "game:/lpt9", "game:/aux", "game:/save.", "game:/save ", "game:/dir./file"})
    {
        CHECK_MESSAGE(vfs.resolve(bad, out) == FileError::InvalidPath, bad);
    }
    for (const char* good :
         {"game:/console.txt", "game:/com10", "game:/nulls/a.txt", "game:/a.b/c.d", "game:/.hidden"})
    {
        CHECK_MESSAGE(vfs.resolve(good, out) == FileError::None, good);
    }
    CHECK(vfs.resolve("other:/a.txt", out) == FileError::UnknownMount);
}

TEST_CASE("write then read text and binary files")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("user", dir.path, MountAccess::ReadWrite) == FileError::None);

    CHECK(vfs.writeText("user:/saves/slot1/save.json", "{\"level\": 3}\n") == FileError::None);
    CHECK(vfs.exists("user:/saves/slot1/save.json"));
    std::string text;
    CHECK(vfs.readText("user:/saves/slot1/save.json", text) == FileError::None);
    CHECK(text == "{\"level\": 3}\n");

    const std::byte bytes[] = {std::byte{0}, std::byte{255}, std::byte{13}, std::byte{10}};
    CHECK(vfs.writeFile("user:/data.bin", bytes) == FileError::None);
    std::vector<std::byte> read;
    CHECK(vfs.readFile("user:/data.bin", read) == FileError::None);
    REQUIRE(read.size() == 4);
    CHECK(read[1] == std::byte{255});
    CHECK(read[3] == std::byte{10});

    // Overwrite replaces atomically and leaves no temporary files behind.
    CHECK(vfs.writeText("user:/data.bin", "") == FileError::None);
    CHECK(vfs.readFile("user:/data.bin", read) == FileError::None);
    CHECK(read.empty());
    std::vector<DirectoryEntry> entries;
    CHECK(vfs.list("user:/", entries) == FileError::None);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "data.bin");
    CHECK_FALSE(entries[0].isDirectory);
    CHECK(entries[1].name == "saves");
    CHECK(entries[1].isDirectory);
}

TEST_CASE("UTF-8 file names work on every platform")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("user", dir.path, MountAccess::ReadWrite) == FileError::None);
    CHECK(vfs.writeText("user:/kayıt-ğüşöç.txt", "merhaba") == FileError::None);
    std::string text;
    CHECK(vfs.readText("user:/kayıt-ğüşöç.txt", text) == FileError::None);
    CHECK(text == "merhaba");
    std::vector<DirectoryEntry> entries;
    REQUIRE(vfs.list("user:/", entries) == FileError::None);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "kayıt-ğüşöç.txt");
    CHECK(entries[0].size == 7);
}

TEST_CASE("read-only mounts reject writes and missing files report NotFound")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("game", dir.path) == FileError::None);
    CHECK(vfs.writeText("game:/x.txt", "nope") == FileError::ReadOnly);
    CHECK_FALSE(vfs.exists("game:/x.txt"));

    std::string text;
    CHECK(vfs.readText("game:/missing.txt", text) == FileError::NotFound);
    std::vector<DirectoryEntry> entries;
    CHECK(vfs.list("game:/missing", entries) == FileError::NotFound);
    CHECK(vfs.readText("game:/", text) == FileError::NotFound); // a directory is not a file
}

TEST_CASE("normalizePath resolves dot components without leaving the mount")
{
    CHECK(VirtualFileSystem::normalizePath("game:/models/../textures/./a.png") == "game:/textures/a.png");
    CHECK(VirtualFileSystem::normalizePath("game:/a//b/") == "game:/a/b");
    CHECK(VirtualFileSystem::normalizePath("game:/") == "game:/");
    CHECK(VirtualFileSystem::normalizePath("game:/a/..") == "game:/");
    CHECK_FALSE(VirtualFileSystem::normalizePath("game:/../secret").has_value());
    CHECK_FALSE(VirtualFileSystem::normalizePath("game:/a/../../b").has_value());
    CHECK_FALSE(VirtualFileSystem::normalizePath("no-mount/a").has_value());
    CHECK_FALSE(VirtualFileSystem::normalizePath("game:/con").has_value());
}

TEST_CASE("utf8Path keeps non-ASCII characters on every platform")
{
    const std::filesystem::path path = utf8Path("klasör/dosya-ğüş.txt");
    CHECK(path.filename().u8string() == u8"dosya-ğüş.txt");
    CHECK(path.parent_path().u8string() == u8"klasör");
}

TEST_CASE("fileErrorName covers every error")
{
    CHECK(fileErrorName(FileError::None) == "None");
    CHECK(fileErrorName(FileError::NotFound) == "NotFound");
    CHECK(fileErrorName(FileError::ReadOnly) == "ReadOnly");
    for (u8 value = 0; value <= static_cast<u8>(FileError::Unsupported); ++value)
    {
        CHECK(fileErrorName(static_cast<FileError>(value)) != "Unknown");
    }
    CHECK(fileErrorName(FileError::Unsupported) == "Unsupported");
}

TEST_CASE("platform directories are absolute")
{
    const std::filesystem::path exe = executableDirectory();
    CHECK(exe.is_absolute());
    CHECK(std::filesystem::is_directory(exe));

    const std::filesystem::path user = userDataDirectory("illusion", "Tests");
    CHECK(user.is_absolute());
    CHECK(user.filename() == "Tests");
    CHECK(user.parent_path().filename() == "illusion");
}

TEST_CASE("stat reports files, directories and missing paths")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("user", dir.path, MountAccess::ReadWrite) == FileError::None);
    REQUIRE(vfs.writeText("user:/a/b.txt", "hello") == FileError::None);

    FileStat stat;
    REQUIRE(vfs.stat("user:/a/b.txt", stat) == FileError::None);
    CHECK_FALSE(stat.isDirectory);
    CHECK(stat.size == 5);
    CHECK(stat.modified != 0);
    REQUIRE(vfs.stat("user:/a", stat) == FileError::None);
    CHECK(stat.isDirectory);
    CHECK(stat.size == 0);
    CHECK(vfs.stat("user:/missing", stat) == FileError::NotFound);
    CHECK(vfs.stat("user:/../x", stat) == FileError::InvalidPath);
    CHECK(vfs.stat("other:/x", stat) == FileError::UnknownMount);
}

TEST_CASE("listRecursive lists every entry below a directory with relative names")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("user", dir.path, MountAccess::ReadWrite) == FileError::None);
    REQUIRE(vfs.writeText("user:/root/b.txt", "b") == FileError::None);
    REQUIRE(vfs.writeText("user:/root/sub/c.txt", "cc") == FileError::None);
    REQUIRE(vfs.createDirectory("user:/root/empty") == FileError::None);

    std::vector<DirectoryEntry> entries;
    REQUIRE(vfs.listRecursive("user:/root", entries) == FileError::None);
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].name == "b.txt");
    CHECK(entries[1].name == "empty");
    CHECK(entries[1].isDirectory);
    CHECK(entries[2].name == "sub");
    CHECK(entries[3].name == "sub/c.txt");
    CHECK(entries[3].size == 2);
    CHECK(vfs.listRecursive("user:/root/b.txt", entries) == FileError::NotFound);
    CHECK(vfs.listRecursive("user:/nothing", entries) == FileError::NotFound);
}

TEST_CASE("createDirectory and remove report AlreadyExists, NotEmpty and NotFound")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    REQUIRE(vfs.mount("user", dir.path, MountAccess::ReadWrite) == FileError::None);

    CHECK(vfs.createDirectory("user:/x/y") == FileError::None);
    CHECK(vfs.exists("user:/x/y"));
    CHECK(vfs.createDirectory("user:/x/y") == FileError::AlreadyExists);
    REQUIRE(vfs.writeText("user:/x/y/file.txt", "1") == FileError::None);
    CHECK(vfs.createDirectory("user:/x/y/file.txt") == FileError::AlreadyExists);

    CHECK(vfs.remove("user:/x") == FileError::NotEmpty);
    CHECK(vfs.remove("user:/x/y/file.txt") == FileError::None);
    CHECK_FALSE(vfs.exists("user:/x/y/file.txt"));
    CHECK(vfs.remove("user:/x/y") == FileError::None);
    CHECK(vfs.remove("user:/x/y") == FileError::NotFound);
    REQUIRE(vfs.writeText("user:/x/deep/a.txt", "1") == FileError::None);
    CHECK(vfs.remove("user:/x", true) == FileError::None);
    CHECK_FALSE(vfs.exists("user:/x"));
    CHECK(vfs.remove("user:/") == FileError::InvalidPath);
    CHECK(vfs.createDirectory("user:/") == FileError::InvalidPath);
}

TEST_CASE("rename moves files and directories within and across writable mounts")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    std::filesystem::create_directories(dir.path / "one");
    std::filesystem::create_directories(dir.path / "two");
    REQUIRE(vfs.mount("one", dir.path / "one", MountAccess::ReadWrite) == FileError::None);
    REQUIRE(vfs.mount("two", dir.path / "two", MountAccess::ReadWrite) == FileError::None);
    REQUIRE(vfs.writeText("one:/a.txt", "a") == FileError::None);
    REQUIRE(vfs.writeText("one:/dir/b.txt", "b") == FileError::None);

    CHECK(vfs.rename("one:/a.txt", "one:/new/place/a2.txt") == FileError::None);
    CHECK_FALSE(vfs.exists("one:/a.txt"));
    std::string text;
    REQUIRE(vfs.readText("one:/new/place/a2.txt", text) == FileError::None);
    CHECK(text == "a");
    CHECK(vfs.rename("one:/dir", "two:/moved") == FileError::None);
    REQUIRE(vfs.readText("two:/moved/b.txt", text) == FileError::None);
    CHECK(text == "b");

    CHECK(vfs.rename("one:/missing", "one:/x") == FileError::NotFound);
    REQUIRE(vfs.writeText("one:/c.txt", "c") == FileError::None);
    CHECK(vfs.rename("one:/c.txt", "one:/new/place/a2.txt") == FileError::AlreadyExists);
    CHECK(vfs.rename("two:/moved", "two:/moved/inner") == FileError::InvalidPath);
    CHECK(vfs.rename("one:/c.txt", "one:/../escape.txt") == FileError::InvalidPath);
}

TEST_CASE("copy duplicates files and directory trees, also out of read-only mounts")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    std::filesystem::create_directories(dir.path / "data");
    std::filesystem::create_directories(dir.path / "user");
    REQUIRE(vfs.mount("rw", dir.path / "data", MountAccess::ReadWrite) == FileError::None);
    REQUIRE(vfs.writeText("rw:/tree/a.txt", "a") == FileError::None);
    REQUIRE(vfs.writeText("rw:/tree/sub/b.txt", "b") == FileError::None);
    REQUIRE(vfs.mount("game", dir.path / "data") == FileError::None);
    REQUIRE(vfs.mount("user", dir.path / "user", MountAccess::ReadWrite) == FileError::None);

    CHECK(vfs.copy("game:/tree/a.txt", "user:/a.txt") == FileError::None);
    CHECK(vfs.copy("game:/tree", "user:/copy") == FileError::None);
    std::string text;
    REQUIRE(vfs.readText("user:/copy/sub/b.txt", text) == FileError::None);
    CHECK(text == "b");
    CHECK(vfs.exists("game:/tree/sub/b.txt"));

    CHECK(vfs.copy("game:/tree", "user:/copy") == FileError::AlreadyExists);
    CHECK(vfs.copy("game:/missing", "user:/x") == FileError::NotFound);
    CHECK(vfs.copy("user:/a.txt", "game:/a.txt") == FileError::ReadOnly);
    CHECK(vfs.copy("rw:/tree", "rw:/tree/inside") == FileError::InvalidPath);
}

TEST_CASE("file management refuses read-only mounts and archives")
{
    TempDirectory dir;
    VirtualFileSystem vfs;
    std::filesystem::create_directories(dir.path / "game");
    REQUIRE(vfs.mount("game", dir.path / "game") == FileError::None);
    std::vector<ArchiveInput> files(1);
    files[0].path = "dir/one.txt";
    files[0].data = {std::byte{'1'}};
    REQUIRE(writeArchive(dir.path / "pack.ilpak", files) == FileError::None);
    REQUIRE(vfs.mountArchive("pak", dir.path / "pack.ilpak") == FileError::None);

    CHECK(vfs.createDirectory("game:/x") == FileError::ReadOnly);
    CHECK(vfs.remove("pak:/dir/one.txt") == FileError::ReadOnly);
    CHECK(vfs.rename("pak:/dir", "game:/dir") == FileError::ReadOnly);

    FileStat stat;
    REQUIRE(vfs.stat("pak:/dir/one.txt", stat) == FileError::None);
    CHECK(stat.size == 1);
    REQUIRE(vfs.stat("pak:/dir", stat) == FileError::None);
    CHECK(stat.isDirectory);
    std::vector<DirectoryEntry> entries;
    REQUIRE(vfs.listRecursive("pak:/", entries) == FileError::None);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "dir");
    CHECK(entries[1].name == "dir/one.txt");
    CHECK(fileErrorName(FileError::AlreadyExists) == "AlreadyExists");
    CHECK(fileErrorName(FileError::NotEmpty) == "NotEmpty");
}
