#include "TestSupport.hpp"

#include <illusion/platform/FileWatcher.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace illusion;

namespace
{

// Writes @p text to @p path and, for the polling backend's determinism, sets its stamp to @p stamp seconds.
void writeFile(const std::filesystem::path& path, const std::string& text, int stamp)
{
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    std::filesystem::last_write_time(
        path, std::filesystem::file_time_type(std::chrono::seconds(1'000'000 + stamp)));
}

std::vector<FileEvent> poll(FileWatcher& watcher)
{
    std::vector<FileEvent> changes;
    watcher.poll(changes);
    return changes;
}

FileWatcher watchPolling(const std::filesystem::path& root)
{
    Expected<FileWatcher, std::string> watcher = FileWatcher::watch(root, FileWatchBackend::Polling);
    REQUIRE(watcher.hasValue());
    return std::move(watcher).value();
}

// Polls a native watcher until @p done(changes) holds for the latest kind of every path seen, failing after
// a generous ceiling (native notifications arrive asynchronously).
template <class Done>
std::map<std::string, FileEventKind> pollUntil(FileWatcher& watcher, Done&& done)
{
    std::map<std::string, FileEventKind> seen;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    std::vector<FileEvent> changes;
    while (!done(seen))
    {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        changes.clear();
        watcher.poll(changes);
        for (const FileEvent& change : changes)
        {
            seen[change.path] = change.kind;
        }
        std::this_thread::yield();
    }
    return seen;
}

bool has(const std::map<std::string, FileEventKind>& seen, const std::string& path, FileEventKind kind)
{
    const auto it = seen.find(path);
    return it != seen.end() && it->second == kind;
}

bool present(const std::map<std::string, FileEventKind>& seen, const std::string& path)
{
    const auto it = seen.find(path);
    return it != seen.end() && it->second != FileEventKind::Removed;
}

} // namespace

TEST_CASE("A default FileWatcher watches nothing and reports nothing")
{
    FileWatcher watcher;
    CHECK_FALSE(watcher.isWatching());
    CHECK(watcher.root().empty());
    CHECK(poll(watcher).empty());
}

TEST_CASE("Watching a missing directory or a file fails")
{
    test::TempDirectory temp;
    CHECK_FALSE(FileWatcher::watch(temp.path / "missing", FileWatchBackend::Native).hasValue());
    writeFile(temp.path / "file.txt", "x", 0);
    CHECK_FALSE(FileWatcher::watch(temp.path / "file.txt", FileWatchBackend::Polling).hasValue());
}

TEST_CASE("A polling watcher reports created, modified and removed files sorted by path")
{
    test::TempDirectory temp;
    writeFile(temp.path / "keep.txt", "keep", 0);
    writeFile(temp.path / "old.txt", "old", 0);
    FileWatcher watcher = watchPolling(temp.path);
    CHECK(watcher.isWatching());
    CHECK(watcher.backend() == FileWatchBackend::Polling);
    CHECK(watcher.root() == temp.path);
    CHECK(poll(watcher).empty());

    writeFile(temp.path / "new.txt", "new", 1);
    writeFile(temp.path / "keep.txt", "keep", 2);
    std::filesystem::remove(temp.path / "old.txt");
    CHECK(poll(watcher) == std::vector<FileEvent>{{FileEventKind::Modified, "keep.txt"},
                                                  {FileEventKind::Created, "new.txt"},
                                                  {FileEventKind::Removed, "old.txt"}});
    CHECK(poll(watcher).empty());

    // A size change alone is a modification too.
    writeFile(temp.path / "new.txt", "newer", 1);
    CHECK(poll(watcher) == std::vector<FileEvent>{{FileEventKind::Modified, "new.txt"}});
}

TEST_CASE("A polling watcher reports directories and their contents but no directory modifications")
{
    test::TempDirectory temp;
    std::filesystem::create_directory(temp.path / "models");
    FileWatcher watcher = watchPolling(temp.path);

    std::filesystem::create_directories(temp.path / "models" / "ships");
    writeFile(temp.path / "models" / "ships" / "a.glb", "a", 0);
    CHECK(poll(watcher) == std::vector<FileEvent>{{FileEventKind::Created, "models/ships"},
                                                  {FileEventKind::Created, "models/ships/a.glb"}});

    std::filesystem::rename(temp.path / "models" / "ships", temp.path / "ships");
    CHECK(poll(watcher) == std::vector<FileEvent>{{FileEventKind::Removed, "models/ships"},
                                                  {FileEventKind::Removed, "models/ships/a.glb"},
                                                  {FileEventKind::Created, "ships"},
                                                  {FileEventKind::Created, "ships/a.glb"}});
}

TEST_CASE("A file replaced by a directory of the same name reports the directory as created")
{
    test::TempDirectory temp;
    writeFile(temp.path / "thing", "file", 0);
    FileWatcher watcher = watchPolling(temp.path);
    std::filesystem::remove(temp.path / "thing");
    std::filesystem::create_directory(temp.path / "thing");
    CHECK(poll(watcher) == std::vector<FileEvent>{{FileEventKind::Created, "thing"}});
}

TEST_CASE("Moving a FileWatcher keeps watching; the moved-from watcher watches nothing")
{
    test::TempDirectory temp;
    FileWatcher first = watchPolling(temp.path);
    FileWatcher second = std::move(first);
    CHECK_FALSE(first.isWatching()); // NOLINT(bugprone-use-after-move): moved-from state is specified.
    writeFile(temp.path / "a.txt", "a", 0);
    CHECK(poll(second) == std::vector<FileEvent>{{FileEventKind::Created, "a.txt"}});
    FileWatcher third;
    third = std::move(second);
    std::filesystem::remove(temp.path / "a.txt");
    CHECK(poll(third) == std::vector<FileEvent>{{FileEventKind::Removed, "a.txt"}});
}

TEST_CASE("Backend and change kind names are stable")
{
    CHECK(fileWatchBackendName(FileWatchBackend::Native) == "Native");
    CHECK(fileWatchBackendName(FileWatchBackend::Polling) == "Polling");
    CHECK(fileEventKindName(FileEventKind::Created) == "Created");
    CHECK(fileEventKindName(FileEventKind::Modified) == "Modified");
    CHECK(fileEventKindName(FileEventKind::Removed) == "Removed");
    CHECK(fileEventKindName(FileEventKind::Overflow) == "Overflow");
}

TEST_CASE("A native watcher reports files created, written, renamed and deleted")
{
    test::TempDirectory temp;
    std::filesystem::create_directory(temp.path / "sub");
    Expected<FileWatcher, std::string> watched = FileWatcher::watch(temp.path, FileWatchBackend::Native);
    REQUIRE(watched.hasValue());
    FileWatcher watcher = std::move(watched).value();

    writeFile(temp.path / "sub" / "a.txt", "a", 0);
    pollUntil(watcher, [](const auto& seen) { return present(seen, "sub/a.txt"); });

    writeFile(temp.path / "sub" / "a.txt", "changed", 1);
    pollUntil(watcher, [](const auto& seen) { return present(seen, "sub/a.txt"); });

    std::filesystem::rename(temp.path / "sub" / "a.txt", temp.path / "b.txt");
    pollUntil(watcher, [](const auto& seen) {
        return has(seen, "sub/a.txt", FileEventKind::Removed) && present(seen, "b.txt");
    });

    std::filesystem::remove(temp.path / "b.txt");
    pollUntil(watcher, [](const auto& seen) { return has(seen, "b.txt", FileEventKind::Removed); });
}

TEST_CASE("A native watcher sees files in a directory created after watching started")
{
    test::TempDirectory temp;
    Expected<FileWatcher, std::string> watched = FileWatcher::watch(temp.path, FileWatchBackend::Native);
    REQUIRE(watched.hasValue());
    FileWatcher watcher = std::move(watched).value();

    std::filesystem::create_directories(temp.path / "new" / "deep");
    pollUntil(watcher, [](const auto& seen) { return present(seen, "new"); });
    // A file written once the directory is known is reported by its own path.
    writeFile(temp.path / "new" / "deep" / "c.txt", "c", 0);
    pollUntil(watcher, [](const auto& seen) { return present(seen, "new/deep/c.txt"); });
}
