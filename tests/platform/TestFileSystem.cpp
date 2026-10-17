#include "TestSupport.hpp"

#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <string>

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
