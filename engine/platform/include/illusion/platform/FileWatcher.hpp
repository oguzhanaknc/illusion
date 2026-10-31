#pragma once
/// @file
/// Watching a directory tree for changes: native notifications (ReadDirectoryChangesW, inotify, FSEvents)
/// or a stat-polling backend, coalesced and delivered at an explicit poll().

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// What happened to a path.
enum class FileEventKind : u8
{
    Created,  ///< A file or directory appeared (created, or renamed or moved in).
    Modified, ///< A file's contents or size changed (directories never report this).
    Removed,  ///< A file or directory disappeared (deleted, or renamed or moved out).
    Overflow, ///< Changes were lost (the system's queue overflowed); rescan the whole tree. Its path is "".
};

/// The name of @p kind ("Created", ...).
[[nodiscard]] std::string_view fileEventKindName(FileEventKind kind) noexcept;

/// One change reported by FileWatcher::poll().
struct FileEvent
{
    FileEventKind kind = FileEventKind::Modified; ///< What happened.
    /// The path relative to the watched root, UTF-8 with '/' separators ("textures/rock.png").
    std::string path{};

    /// Changes compare by kind and path.
    friend bool operator==(const FileEvent&, const FileEvent&) = default;
};

/// How a FileWatcher learns about changes.
enum class FileWatchBackend : u8
{
    /// The operating system's notifications: ReadDirectoryChangesW on Windows, inotify on Linux and FSEvents
    /// on macOS. Falls back to Polling where they are unavailable (network drives, exhausted inotify watch
    /// limits).
    Native,
    /// Every poll() lists the tree and compares sizes and modification stamps with the previous listing.
    /// Works everywhere and is deterministic, but costs a directory walk per poll().
    Polling,
};

/// The name of @p backend ("Native" or "Polling").
[[nodiscard]] std::string_view fileWatchBackendName(FileWatchBackend backend) noexcept;

/// Watches every file and directory below a root directory.
///
/// Changes are collected as they arrive (by the system or a backend thread) and handed out by poll(),
/// coalesced per path: a file created and then written is one Created, one created and deleted again
/// between two polls is nothing, one deleted and created again is a Modified (a Created if a directory
/// took the place of a file). Renames are reported as a
/// Removed of the old path and a Created of the new one. A directory that appears or disappears is one
/// change; the files inside it are not always reported separately, so treat a directory change as a change
/// of everything below it. Hidden files are reported like any other.
///
/// Not thread-safe: call poll() from one thread at a time.
class FileWatcher
{
public:
    /// A watcher that watches nothing; poll() reports nothing.
    FileWatcher() noexcept;
    /// Takes over @p other, which then watches nothing.
    FileWatcher(FileWatcher&& other) noexcept;
    /// Stops watching and takes over @p other.
    FileWatcher& operator=(FileWatcher&& other) noexcept;
    FileWatcher(const FileWatcher&) = delete;            ///< Not copyable.
    FileWatcher& operator=(const FileWatcher&) = delete; ///< Not copyable.
    /// Stops watching.
    ~FileWatcher();

    /// Starts watching the directory @p root and everything below it with @p backend. A Native watcher
    /// that the system cannot provide falls back to Polling (see backend()). The error says why @p root
    /// cannot be watched (it is missing or not a directory).
    [[nodiscard]] static Expected<FileWatcher, std::string> watch(const std::filesystem::path& root,
                                                                  FileWatchBackend backend);

    /// True if this watcher watches a directory.
    [[nodiscard]] bool isWatching() const noexcept { return m_impl != nullptr; }

    /// The backend in use, which is Polling when a Native watcher fell back.
    [[nodiscard]] FileWatchBackend backend() const noexcept;

    /// The watched directory (empty if this watcher watches nothing).
    [[nodiscard]] const std::filesystem::path& root() const noexcept;

    /// Appends the changes since the previous poll() to @p out, at most one per path, sorted by path. An
    /// Overflow comes first, and after it the changes that were not lost.
    void poll(std::vector<FileEvent>& out);

private:
    struct Impl;
    explicit FileWatcher(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
