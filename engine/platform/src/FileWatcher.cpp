#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/FileWatcher.hpp>

#include <chrono>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(ILL_PLATFORM_MACOS)
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#else
#include <cerrno>
#include <climits>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>
#include <unordered_map>
#endif

namespace illusion
{
namespace
{

// Changes collected between two polls, coalesced per path.
class ChangeSet
{
public:
    void record(FileEventKind kind, std::string path)
    {
        if (path.empty())
        {
            return;
        }
        const auto it = m_changes.find(path);
        if (it == m_changes.end())
        {
            m_changes.emplace(std::move(path), Pending{kind, false});
            return;
        }
        Pending& pending = it->second;
        if (pending.kind == FileEventKind::Created)
        {
            if (kind == FileEventKind::Removed)
            {
                m_changes.erase(it);
            }
            return;
        }
        if (kind == FileEventKind::Removed)
        {
            pending = {FileEventKind::Removed, false};
            return;
        }
        pending.replaced = pending.replaced || pending.kind == FileEventKind::Removed;
        pending.kind = FileEventKind::Modified;
    }

    void overflow() noexcept { m_overflow = true; }

    // Moves the changes to @p out (see FileWatcher::poll()). @p isDirectory(path) tells directories, whose
    // modifications are dropped, except when one replaced a removed path: that is reported as created.
    template <class IsDirectory>
    void drain(std::vector<FileEvent>& out, IsDirectory&& isDirectory)
    {
        if (m_overflow)
        {
            out.push_back({FileEventKind::Overflow, {}});
        }
        for (auto& [path, pending] : m_changes)
        {
            FileEventKind kind = pending.kind;
            if (kind == FileEventKind::Modified && isDirectory(path))
            {
                if (!pending.replaced)
                {
                    continue;
                }
                kind = FileEventKind::Created;
            }
            out.push_back({kind, path});
        }
        m_changes.clear();
        m_overflow = false;
    }

private:
    struct Pending
    {
        FileEventKind kind;
        bool replaced; // Removed, then created again.
    };

    std::map<std::string, Pending, std::less<>> m_changes;
    bool m_overflow = false;
};

std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool isDirectoryOnDisk(const std::filesystem::path& path)
{
    std::error_code code;
    return std::filesystem::is_directory(path, code);
}

// What the polling backend remembers about one path.
struct PolledEntry
{
    bool isDirectory = false;
    u64 size = 0;
    i64 modified = 0;

    friend bool operator==(const PolledEntry&, const PolledEntry&) = default;
};

using PolledTree = std::map<std::string, PolledEntry, std::less<>>;

PolledTree listTree(const std::filesystem::path& root)
{
    PolledTree tree;
    std::error_code code;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, code), end; !code && it != end;
         it.increment(code))
    {
        PolledEntry entry;
        std::error_code entryCode;
        entry.isDirectory = it->is_directory(entryCode);
        if (!entry.isDirectory)
        {
            entry.size = static_cast<u64>(it->file_size(entryCode));
            const std::filesystem::file_time_type time = it->last_write_time(entryCode);
            entry.modified =
                entryCode ? 0
                          : static_cast<i64>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch())
                                    .count());
        }
        tree.emplace(utf8Of(it->path().lexically_relative(root)), entry);
    }
    return tree;
}

// Records the differences from @p before to @p after in @p changes.
void compareTrees(const PolledTree& before, const PolledTree& after, ChangeSet& changes)
{
    auto old = before.begin();
    auto now = after.begin();
    while (old != before.end() || now != after.end())
    {
        if (now == after.end() || (old != before.end() && old->first < now->first))
        {
            changes.record(FileEventKind::Removed, old->first);
            ++old;
        }
        else if (old == before.end() || now->first < old->first)
        {
            changes.record(FileEventKind::Created, now->first);
            ++now;
        }
        else
        {
            if (old->second.isDirectory != now->second.isDirectory)
            {
                changes.record(FileEventKind::Removed, old->first);
                changes.record(FileEventKind::Created, now->first);
            }
            else if (!now->second.isDirectory && old->second != now->second)
            {
                changes.record(FileEventKind::Modified, now->first);
            }
            ++old;
            ++now;
        }
    }
}

#if defined(ILL_PLATFORM_WINDOWS)

std::string utf8OfWide(const wchar_t* text, usize length)
{
    return utf8Of(std::filesystem::path(std::wstring(text, length)));
}

// ReadDirectoryChangesW on an overlapped directory handle, completed without blocking in collect().
class NativeWatch
{
public:
    static std::unique_ptr<NativeWatch> open(const std::filesystem::path& root, std::string& error)
    {
        auto watch = std::unique_ptr<NativeWatch>(new NativeWatch());
        watch->m_directory = CreateFileW(
            root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (watch->m_directory == INVALID_HANDLE_VALUE)
        {
            error = "CreateFileW failed (" + std::to_string(GetLastError()) + ")";
            return nullptr;
        }
        watch->m_overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (watch->m_overlapped.hEvent == nullptr)
        {
            error = "CreateEventW failed (" + std::to_string(GetLastError()) + ")";
            return nullptr;
        }
        if (!watch->issue())
        {
            error = "ReadDirectoryChangesW failed (" + std::to_string(GetLastError()) + ")";
            return nullptr;
        }
        return watch;
    }

    NativeWatch(const NativeWatch&) = delete;
    NativeWatch& operator=(const NativeWatch&) = delete;

    ~NativeWatch()
    {
        if (m_pending)
        {
            CancelIoEx(m_directory, &m_overlapped);
            DWORD bytes = 0;
            GetOverlappedResult(m_directory, &m_overlapped, &bytes, TRUE);
        }
        if (m_overlapped.hEvent != nullptr)
        {
            CloseHandle(m_overlapped.hEvent);
        }
        if (m_directory != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_directory);
        }
    }

    void collect(ChangeSet& changes)
    {
        while (m_pending)
        {
            DWORD bytes = 0;
            if (!GetOverlappedResult(m_directory, &m_overlapped, &bytes, FALSE))
            {
                if (GetLastError() == ERROR_IO_INCOMPLETE)
                {
                    return;
                }
                m_pending = false;
                changes.overflow();
            }
            else
            {
                m_pending = false;
                if (bytes == 0)
                {
                    changes.overflow(); // The system's buffer overflowed.
                }
                else
                {
                    parse(changes);
                }
            }
            if (!issue())
            {
                ILL_LOG_WARN("Platform", "file watcher stopped: ReadDirectoryChangesW failed ({})",
                             GetLastError());
                changes.overflow();
            }
        }
    }

private:
    NativeWatch() = default;

    bool issue()
    {
        ResetEvent(m_overlapped.hEvent);
        constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                  FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                  FILE_NOTIFY_CHANGE_CREATION;
        m_pending = ReadDirectoryChangesW(m_directory, m_buffer.data(),
                                          static_cast<DWORD>(sizeof(DWORD) * m_buffer.size()), TRUE, kFilter,
                                          nullptr, &m_overlapped, nullptr) != FALSE;
        return m_pending;
    }

    void parse(ChangeSet& changes)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(m_buffer.data());
        for (usize offset = 0;;)
        {
            const auto* info =
                static_cast<const FILE_NOTIFY_INFORMATION*>(static_cast<const void*>(bytes + offset));
            std::string path = utf8OfWide(info->FileName, info->FileNameLength / sizeof(wchar_t));
            switch (info->Action)
            {
            case FILE_ACTION_ADDED:
            case FILE_ACTION_RENAMED_NEW_NAME:
                changes.record(FileEventKind::Created, std::move(path));
                break;
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME:
                changes.record(FileEventKind::Removed, std::move(path));
                break;
            default:
                changes.record(FileEventKind::Modified, std::move(path));
                break;
            }
            if (info->NextEntryOffset == 0)
            {
                return;
            }
            offset += info->NextEntryOffset;
        }
    }

    HANDLE m_directory = INVALID_HANDLE_VALUE;
    OVERLAPPED m_overlapped{};
    bool m_pending = false;
    std::vector<DWORD> m_buffer = std::vector<DWORD>(16384); // 64 KiB, the limit for network shares.
};

#elif defined(ILL_PLATFORM_MACOS)

// An FSEvents stream with file-level events on a private dispatch queue; its callback queues raw events.
class NativeWatch
{
public:
    static std::unique_ptr<NativeWatch> open(const std::filesystem::path& root, std::string& error)
    {
        auto watch = std::unique_ptr<NativeWatch>(new NativeWatch());
        // FSEvents reports real paths (/private/var/..., not /var/...).
        std::error_code code;
        watch->m_root = utf8Of(std::filesystem::canonical(root, code));
        if (code)
        {
            error = "cannot resolve the directory (" + code.message() + ")";
            return nullptr;
        }
        CFStringRef path = CFStringCreateWithFileSystemRepresentation(nullptr, watch->m_root.c_str());
        const void* values[] = {path};
        CFArrayRef paths = CFArrayCreate(nullptr, values, 1, &kCFTypeArrayCallBacks);
        FSEventStreamContext context{0, watch.get(), nullptr, nullptr, nullptr};
        watch->m_stream = FSEventStreamCreate(
            nullptr, &NativeWatch::callback, &context, paths, kFSEventStreamEventIdSinceNow, 0.02,
            static_cast<FSEventStreamCreateFlags>(kFSEventStreamCreateFlagFileEvents |
                                                  kFSEventStreamCreateFlagNoDefer));
        CFRelease(paths);
        CFRelease(path);
        if (watch->m_stream == nullptr)
        {
            error = "FSEventStreamCreate failed";
            return nullptr;
        }
        watch->m_queue = dispatch_queue_create("illusion.filewatcher", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(watch->m_stream, watch->m_queue);
        if (!FSEventStreamStart(watch->m_stream))
        {
            error = "FSEventStreamStart failed";
            return nullptr;
        }
        watch->m_started = true;
        return watch;
    }

    NativeWatch(const NativeWatch&) = delete;
    NativeWatch& operator=(const NativeWatch&) = delete;

    ~NativeWatch()
    {
        if (m_stream != nullptr)
        {
            if (m_started)
            {
                FSEventStreamStop(m_stream);
            }
            FSEventStreamInvalidate(m_stream);
            FSEventStreamRelease(m_stream);
        }
        if (m_queue != nullptr)
        {
            // Wait for a callback that may still run on the queue.
            dispatch_sync_f(m_queue, nullptr, [](void*) {});
            dispatch_release(m_queue);
        }
    }

    void collect(ChangeSet& changes)
    {
        std::vector<RawEvent> events;
        {
            const std::scoped_lock lock(m_mutex);
            events.swap(m_events);
            if (m_overflow)
            {
                changes.overflow();
                m_overflow = false;
            }
        }
        const std::filesystem::path root = utf8Path(m_root);
        for (RawEvent& event : events)
        {
            // Flags accumulate over an item's recent history, so the file system decides what happened.
            std::error_code code;
            const bool exists = std::filesystem::exists(root / utf8Path(event.path), code);
            if (!exists)
            {
                changes.record(FileEventKind::Removed, std::move(event.path));
            }
            else if ((event.flags & kAppeared) != 0)
            {
                changes.record(FileEventKind::Created, std::move(event.path));
            }
            else
            {
                changes.record(FileEventKind::Modified, std::move(event.path));
            }
        }
    }

private:
    struct RawEvent
    {
        std::string path;
        FSEventStreamEventFlags flags = 0;
    };

    static constexpr auto kAppeared = static_cast<FSEventStreamEventFlags>(
        kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRenamed);
    static constexpr auto kLost = static_cast<FSEventStreamEventFlags>(
        kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped |
        kFSEventStreamEventFlagKernelDropped);

    NativeWatch() = default;

    static void callback(ConstFSEventStreamRef, void* info, size_t count, void* paths,
                         const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
    {
        auto& watch = *static_cast<NativeWatch*>(info);
        const auto* const* names = static_cast<const char* const*>(paths);
        const std::scoped_lock lock(watch.m_mutex);
        for (size_t i = 0; i < count; ++i)
        {
            if ((flags[i] & kLost) != 0)
            {
                watch.m_overflow = true;
            }
            const std::string_view name = names[i];
            if (name.size() <= watch.m_root.size() + 1 || !name.starts_with(watch.m_root) ||
                name[watch.m_root.size()] != '/')
            {
                continue; // The root itself, or outside it.
            }
            watch.m_events.push_back({std::string(name.substr(watch.m_root.size() + 1)), flags[i]});
        }
    }

    std::string m_root;
    FSEventStreamRef m_stream = nullptr;
    dispatch_queue_t m_queue = nullptr;
    bool m_started = false;
    std::mutex m_mutex;
    std::vector<RawEvent> m_events;
    bool m_overflow = false;
};

#else

// inotify with one watch per directory, read without blocking in collect().
class NativeWatch
{
public:
    static std::unique_ptr<NativeWatch> open(const std::filesystem::path& root, std::string& error)
    {
        auto watch = std::unique_ptr<NativeWatch>(new NativeWatch());
        watch->m_root = root;
        watch->m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (watch->m_fd < 0)
        {
            error = "inotify_init1 failed (" + std::string(std::strerror(errno)) + ")";
            return nullptr;
        }
        if (!watch->addTree({}, nullptr))
        {
            error = "inotify_add_watch failed (" + std::string(std::strerror(errno)) + ")";
            return nullptr;
        }
        return watch;
    }

    NativeWatch(const NativeWatch&) = delete;
    NativeWatch& operator=(const NativeWatch&) = delete;

    ~NativeWatch()
    {
        if (m_fd >= 0)
        {
            close(m_fd);
        }
    }

    void collect(ChangeSet& changes)
    {
        alignas(inotify_event) char buffer[16384];
        for (;;)
        {
            const ssize_t length = read(m_fd, buffer, sizeof(buffer));
            if (length <= 0)
            {
                return; // EAGAIN: nothing more to read.
            }
            for (ssize_t offset = 0; offset < length;)
            {
                const auto* event =
                    static_cast<const inotify_event*>(static_cast<const void*>(buffer + offset));
                handle(*event, changes);
                offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
            }
        }
    }

private:
    NativeWatch() = default;

    static constexpr u32 kMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM |
                                 IN_MOVED_TO | IN_EXCL_UNLINK | IN_ONLYDIR;

    void handle(const inotify_event& event, ChangeSet& changes)
    {
        if ((event.mask & IN_Q_OVERFLOW) != 0)
        {
            changes.overflow();
            return;
        }
        if ((event.mask & IN_IGNORED) != 0)
        {
            m_directories.erase(event.wd);
            return;
        }
        const auto directory = m_directories.find(event.wd);
        if (directory == m_directories.end() || event.len == 0)
        {
            return;
        }
        std::string path =
            directory->second.empty() ? std::string(event.name) : directory->second + "/" + event.name;
        const bool isDirectory = (event.mask & IN_ISDIR) != 0;
        if ((event.mask & (IN_CREATE | IN_MOVED_TO)) != 0)
        {
            if (isDirectory && !addTree(path, &changes))
            {
                ILL_LOG_WARN("Platform", "file watcher cannot watch {}: {}", path, std::strerror(errno));
                changes.overflow();
            }
            changes.record(FileEventKind::Created, std::move(path));
        }
        else if ((event.mask & (IN_DELETE | IN_MOVED_FROM)) != 0)
        {
            if (isDirectory)
            {
                forgetTree(path);
            }
            changes.record(FileEventKind::Removed, std::move(path));
        }
        else if (!isDirectory)
        {
            changes.record(FileEventKind::Modified, std::move(path));
        }
    }

    // Watches the directory @p relative and every directory below it. With @p changes, also records what
    // is already inside as created (it may have appeared before the watches existed).
    bool addTree(const std::string& relative, ChangeSet* changes)
    {
        if (!addWatch(relative))
        {
            return false;
        }
        const std::filesystem::path base = relative.empty() ? m_root : m_root / utf8Path(relative);
        std::error_code code;
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(base, options, code), end; !code && it != end;
             it.increment(code))
        {
            const std::string path = utf8Of(it->path().lexically_relative(m_root));
            std::error_code entryCode;
            if (it->is_directory(entryCode) && !addWatch(path))
            {
                return false;
            }
            if (changes != nullptr)
            {
                changes->record(FileEventKind::Created, path);
            }
        }
        return true;
    }

    bool addWatch(const std::string& relative)
    {
        const std::filesystem::path path = relative.empty() ? m_root : m_root / utf8Path(relative);
        const int wd = inotify_add_watch(m_fd, path.c_str(), kMask);
        if (wd < 0)
        {
            // A directory removed again before it could be watched is not an error.
            return errno == ENOENT || errno == ENOTDIR;
        }
        m_directories[wd] = relative; // The same directory moved keeps its watch: update its path.
        return true;
    }

    // Stops watching the directory @p relative (moved or deleted) and everything below it.
    void forgetTree(const std::string& relative)
    {
        for (auto it = m_directories.begin(); it != m_directories.end();)
        {
            const std::string& path = it->second;
            if (path == relative ||
                (path.size() > relative.size() && path.starts_with(relative) && path[relative.size()] == '/'))
            {
                inotify_rm_watch(m_fd, it->first);
                it = m_directories.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::filesystem::path m_root;
    int m_fd = -1;
    std::unordered_map<int, std::string> m_directories;
};

#endif

} // namespace

struct FileWatcher::Impl
{
    std::filesystem::path root;
    FileWatchBackend backend = FileWatchBackend::Polling;
    std::unique_ptr<NativeWatch> native;
    PolledTree polled;
    ChangeSet changes;
};

std::string_view fileEventKindName(FileEventKind kind) noexcept
{
    switch (kind)
    {
    case FileEventKind::Created:
        return "Created";
    case FileEventKind::Modified:
        return "Modified";
    case FileEventKind::Removed:
        return "Removed";
    case FileEventKind::Overflow:
        return "Overflow";
    }
    return "Unknown";
}

std::string_view fileWatchBackendName(FileWatchBackend backend) noexcept
{
    return backend == FileWatchBackend::Native ? "Native" : "Polling";
}

FileWatcher::FileWatcher() noexcept = default;
FileWatcher::FileWatcher(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}
FileWatcher::FileWatcher(FileWatcher&& other) noexcept = default;
FileWatcher& FileWatcher::operator=(FileWatcher&& other) noexcept = default;
FileWatcher::~FileWatcher() = default;

Expected<FileWatcher, std::string> FileWatcher::watch(const std::filesystem::path& root,
                                                      FileWatchBackend backend)
{
    std::error_code code;
    if (!std::filesystem::is_directory(root, code))
    {
        return unexpected("'" + utf8Of(root) + "' is not a directory");
    }
    auto impl = std::make_unique<Impl>();
    impl->root = root;
    if (backend == FileWatchBackend::Native)
    {
        std::string error;
        impl->native = NativeWatch::open(root, error);
        if (impl->native)
        {
            impl->backend = FileWatchBackend::Native;
        }
        else
        {
            ILL_LOG_WARN("Platform", "native file watching of {} is unavailable ({}); polling instead",
                         utf8Of(root), error);
        }
    }
    if (impl->backend == FileWatchBackend::Polling)
    {
        impl->polled = listTree(root);
    }
    return FileWatcher(std::move(impl));
}

FileWatchBackend FileWatcher::backend() const noexcept
{
    return m_impl ? m_impl->backend : FileWatchBackend::Polling;
}

const std::filesystem::path& FileWatcher::root() const noexcept
{
    static const std::filesystem::path kNone;
    return m_impl ? m_impl->root : kNone;
}

void FileWatcher::poll(std::vector<FileEvent>& out)
{
    ILL_PROFILE_SCOPE();
    if (!m_impl)
    {
        return;
    }
    Impl& impl = *m_impl;
    if (impl.native)
    {
        impl.native->collect(impl.changes);
        impl.changes.drain(
            out, [&](const std::string& path) { return isDirectoryOnDisk(impl.root / utf8Path(path)); });
        return;
    }
    PolledTree now = listTree(impl.root);
    compareTrees(impl.polled, now, impl.changes);
    impl.polled = std::move(now);
    impl.changes.drain(out, [&](const std::string& path) {
        const auto it = impl.polled.find(path);
        return it != impl.polled.end() && it->second.isDirectory;
    });
}

} // namespace illusion
