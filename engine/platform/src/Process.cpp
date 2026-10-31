#include <illusion/core/Assert.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(ILL_PLATFORM_MACOS)
#include <crt_externs.h>
#endif
#endif

namespace illusion
{
namespace
{

constexpr usize kReadChunk = 64 * 1024;

// The bytes of one captured stream: the reader thread appends, the owner takes. A full buffer blocks the
// reader (and so, through the pipe, the child) until the owner takes some.
class StreamBuffer
{
public:
    void setCapacity(usize capacity) noexcept { m_capacity = std::max<usize>(capacity, 1); }

    [[nodiscard]] usize chunkSize() const noexcept { return std::min(kReadChunk, m_capacity); }

    // Reader side: appends @p size bytes as room becomes free; false if shutdown() was called.
    bool append(const char* bytes, usize size)
    {
        std::unique_lock lock(m_mutex);
        while (size > 0)
        {
            m_writable.wait(lock, [&] { return m_stop || buffered() < m_capacity; });
            if (m_stop)
            {
                return false;
            }
            compact();
            const usize count = std::min(size, m_capacity - buffered());
            m_data.append(bytes, count);
            bytes += count;
            size -= count;
            m_readable.notify_all();
        }
        return true;
    }

    void finish()
    {
        const std::lock_guard lock(m_mutex);
        m_eof = true;
        m_readable.notify_all();
    }

    void shutdown()
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
        m_writable.notify_all();
    }

    [[nodiscard]] bool finished() const
    {
        const std::lock_guard lock(m_mutex);
        return m_eof;
    }

    // Waits until finish() was called or @p timeout passed; true if finished.
    bool waitFinished(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_readable.wait_for(lock, timeout, [&] { return m_eof; });
    }

    usize take(std::string& out, usize maxBytes)
    {
        const std::lock_guard lock(m_mutex);
        const usize count = std::min(maxBytes, buffered());
        out.append(m_data, m_head, count);
        m_head += count;
        m_scanned = std::max(m_scanned, m_head);
        m_writable.notify_all();
        return count;
    }

    bool takeLine(std::string& line, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        const auto deadline = timeout == std::chrono::milliseconds::max()
                                  ? std::chrono::steady_clock::time_point::max()
                                  : std::chrono::steady_clock::now() + timeout;
        bool timedOut = false;
        for (;;)
        {
            const usize newline = m_data.find('\n', std::max(m_scanned, m_head));
            if (newline != std::string::npos)
            {
                popLine(line, newline, newline + 1);
                return true;
            }
            m_scanned = m_data.size();
            // A line longer than the buffer cannot complete: hand out what fits.
            if (buffered() > 0 && (m_eof || buffered() >= m_capacity))
            {
                popLine(line, m_data.size(), m_data.size());
                return true;
            }
            if (m_eof || timedOut)
            {
                return false;
            }
            if (deadline == std::chrono::steady_clock::time_point::max())
            {
                m_readable.wait(lock);
            }
            else
            {
                timedOut = m_readable.wait_until(lock, deadline) == std::cv_status::timeout;
            }
        }
    }

    [[nodiscard]] bool drained() const
    {
        const std::lock_guard lock(m_mutex);
        return m_eof && buffered() == 0;
    }

private:
    [[nodiscard]] usize buffered() const noexcept { return m_data.size() - m_head; }

    void compact()
    {
        if (m_head > 0 && m_head * 2 >= m_data.size())
        {
            m_data.erase(0, m_head);
            m_scanned -= std::min(m_scanned, m_head);
            m_head = 0;
        }
    }

    void popLine(std::string& line, usize end, usize next)
    {
        line.assign(m_data, m_head, end - m_head);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        m_head = next;
        m_scanned = m_head;
        compact();
        m_writable.notify_all();
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_readable;
    std::condition_variable m_writable;
    std::string m_data;
    usize m_head = 0;    // bytes before this were taken
    usize m_scanned = 0; // no '\n' before this offset
    usize m_capacity = 1;
    bool m_eof = false;
    bool m_stop = false;
};

std::string quoteWindowsArgument(std::string_view argument)
{
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos)
    {
        return std::string(argument);
    }
    std::string out = "\"";
    for (usize i = 0; i < argument.size(); ++i)
    {
        usize backslashes = 0;
        while (i < argument.size() && argument[i] == '\\')
        {
            ++backslashes;
            ++i;
        }
        if (i == argument.size())
        {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (argument[i] == '"')
        {
            out.append(backslashes * 2 + 1, '\\');
        }
        else
        {
            out.append(backslashes, '\\');
        }
        out += argument[i];
    }
    out += '"';
    return out;
}

std::string utf8FromPath(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

#if defined(ILL_PLATFORM_WINDOWS)

std::wstring widen(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<usize>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string lastErrorText(std::string_view what)
{
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

// Case-insensitive comparison of environment variable names, as Windows defines it.
bool sameName(std::wstring_view a, std::wstring_view b)
{
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

std::wstring_view nameOf(std::wstring_view entry)
{
    // "=C:=C:\dir" entries start with '=': their name runs to the second '='.
    const usize equals = entry.find(L'=', 1);
    return entry.substr(0, equals == std::wstring_view::npos ? entry.size() : equals);
}

std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& overrides)
{
    std::vector<std::wstring> entries;
    if (wchar_t* strings = GetEnvironmentStringsW(); strings != nullptr)
    {
        for (const wchar_t* entry = strings; *entry != L'\0'; entry += std::wcslen(entry) + 1)
        {
            entries.emplace_back(entry);
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& [name, value] : overrides)
    {
        const std::wstring wideName = widen(name);
        std::erase_if(entries, [&](const std::wstring& entry) { return sameName(nameOf(entry), wideName); });
        entries.push_back(wideName + L"=" + widen(value));
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        const std::wstring_view nameA = nameOf(a);
        const std::wstring_view nameB = nameOf(b);
        return CompareStringOrdinal(nameA.data(), static_cast<int>(nameA.size()), nameB.data(),
                                    static_cast<int>(nameB.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::wstring block;
    for (const std::wstring& entry : entries)
    {
        block += entry;
        block += L'\0';
    }
    block += L'\0';
    return block;
}

class Handle
{
public:
    Handle() noexcept = default;
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr))
    {
    }
    Handle& operator=(Handle&& other) noexcept
    {
        reset(std::exchange(other.m_handle, nullptr));
        return *this;
    }

    void reset(HANDLE handle = nullptr) noexcept
    {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_handle);
        }
        m_handle = handle;
    }
    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }
    [[nodiscard]] HANDLE* out() noexcept { return &m_handle; }
    [[nodiscard]] HANDLE release() noexcept { return std::exchange(m_handle, nullptr); }

private:
    HANDLE m_handle = nullptr;
};

#else

std::once_flag g_sigpipeOnce;

void ignoreSigpipe()
{
    std::call_once(g_sigpipeOnce, [] {
        struct sigaction current = {};
        if (sigaction(SIGPIPE, nullptr, &current) == 0 && current.sa_handler == SIG_DFL)
        {
            struct sigaction ignore = {};
            ignore.sa_handler = SIG_IGN;
            sigemptyset(&ignore.sa_mask);
            sigaction(SIGPIPE, &ignore, nullptr);
        }
    });
}

char** currentEnvironment()
{
#if defined(ILL_PLATFORM_MACOS)
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

bool makePipe(int fds[2])
{
#if defined(ILL_PLATFORM_MACOS)
    // macOS has no pipe2(); POSIX_SPAWN_CLOEXEC_DEFAULT keeps these from leaking into children.
    if (pipe(fds) != 0)
    {
        return false;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#else
    return pipe2(fds, O_CLOEXEC) == 0;
#endif
}

void closeFd(int& fd) noexcept
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

std::string errnoText(std::string_view what, int error)
{
    return std::string(what) + " failed: " + std::strerror(error);
}

#endif

} // namespace

std::string windowsCommandLine(const std::vector<std::string>& arguments)
{
    std::string out;
    for (const std::string& argument : arguments)
    {
        if (!out.empty())
        {
            out += ' ';
        }
        out += quoteWindowsArgument(argument);
    }
    return out;
}

struct Process::Impl
{
    struct Reader
    {
        StreamBuffer buffer;
        std::thread thread;
        bool captured = false;
#if defined(ILL_PLATFORM_WINDOWS)
        Handle pipe;
#else
        int pipe = -1;
        int wake[2] = {-1, -1}; // written to stop the reader
#endif
    };

    Reader output;
    Reader error;
    std::mutex inputMutex;
    u64 pid = 0;
    bool detached = false; // ProcessOptions::detached: never terminated or waited for

#if defined(ILL_PLATFORM_WINDOWS)
    Handle process;
    Handle input;
#else
    int input = -1;
    std::thread waiter;
    std::mutex exitMutex;
    std::condition_variable exitChanged;
    bool exited = false;
    i32 exitCode = 0;
#endif

    Reader& reader(ProcessStream stream) noexcept { return stream == ProcessStream::Output ? output : error; }

    void startReader(Reader& reader);
    void stopReader(Reader& reader);
    std::optional<i32> waitFor(std::chrono::milliseconds timeout);
    bool kill() noexcept;

    ~Impl()
    {
        if (!detached)
        {
            kill();
            waitFor(std::chrono::milliseconds::max());
        }
        stopReader(output);
        stopReader(error);
        const std::lock_guard lock(inputMutex);
#if defined(ILL_PLATFORM_WINDOWS)
        input.reset();
#else
        if (waiter.joinable())
        {
            waiter.join();
        }
        closeFd(input);
#endif
    }
};

#if defined(ILL_PLATFORM_WINDOWS)

void Process::Impl::startReader(Reader& reader)
{
    reader.thread = std::thread([&reader] {
        std::string chunk(reader.buffer.chunkSize(), '\0');
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(reader.pipe.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                          nullptr) ||
                read == 0)
            {
                break;
            }
            if (!reader.buffer.append(chunk.data(), read))
            {
                break;
            }
        }
        reader.buffer.finish();
    });
}

void Process::Impl::stopReader(Reader& reader)
{
    if (!reader.thread.joinable())
    {
        return;
    }
    reader.buffer.shutdown();
    // A grandchild may still hold the pipe open: cancel the blocking ReadFile until the thread ends.
    while (!reader.buffer.waitFinished(std::chrono::milliseconds(1)))
    {
        CancelSynchronousIo(reader.thread.native_handle());
    }
    reader.thread.join();
    reader.pipe.reset();
}

std::optional<i32> Process::Impl::waitFor(std::chrono::milliseconds timeout)
{
    if (process.get() == nullptr)
    {
        return std::nullopt;
    }
    const DWORD milliseconds =
        timeout == std::chrono::milliseconds::max()
            ? INFINITE
            : static_cast<DWORD>(std::clamp<i64>(timeout.count(), 0, static_cast<i64>(INFINITE - 1)));
    if (WaitForSingleObject(process.get(), milliseconds) != WAIT_OBJECT_0)
    {
        return std::nullopt;
    }
    DWORD code = 0;
    GetExitCodeProcess(process.get(), &code);
    return static_cast<i32>(code);
}

bool Process::Impl::kill() noexcept
{
    if (process.get() == nullptr || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT)
    {
        return false;
    }
    return TerminateProcess(process.get(), static_cast<UINT>(kTerminatedExitCode)) != 0;
}

Expected<Process, std::string> Process::spawn(const ProcessOptions& options)
{
    std::error_code code;
    if (!std::filesystem::is_regular_file(options.executable, code))
    {
        return unexpected("executable '" + utf8FromPath(options.executable) + "' does not exist");
    }

    auto impl = std::make_unique<Impl>();
    SECURITY_ATTRIBUTES inherit = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle childInput, childOutput, childError;

    if (options.pipeInput && !options.detached)
    {
        if (!CreatePipe(childInput.out(), impl->input.out(), &inherit, 0))
        {
            return unexpected(lastErrorText("CreatePipe"));
        }
        SetHandleInformation(impl->input.get(), HANDLE_FLAG_INHERIT, 0);
    }
    else
    {
        childInput.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                     OPEN_EXISTING, 0, nullptr));
    }

    const auto prepareOutput = [&](Impl::Reader& reader, bool capture, DWORD standard,
                                   Handle& child) -> bool {
        reader.captured = capture;
        if (capture)
        {
            reader.buffer.setCapacity(options.bufferCapacity);
            if (!CreatePipe(reader.pipe.out(), child.out(), &inherit, 0))
            {
                return false;
            }
            SetHandleInformation(reader.pipe.get(), HANDLE_FLAG_INHERIT, 0);
            return true;
        }
        reader.buffer.finish();
        const HANDLE own = options.detached ? nullptr : GetStdHandle(standard);
        if (own == nullptr || own == INVALID_HANDLE_VALUE ||
            !DuplicateHandle(GetCurrentProcess(), own, GetCurrentProcess(), child.out(), 0, TRUE,
                             DUPLICATE_SAME_ACCESS))
        {
            child.reset(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                    OPEN_EXISTING, 0, nullptr));
        }
        return true;
    };
    if (!prepareOutput(impl->output, options.captureOutput && !options.detached, STD_OUTPUT_HANDLE,
                       childOutput) ||
        !prepareOutput(impl->error, options.captureError && !options.detached, STD_ERROR_HANDLE, childError))
    {
        return unexpected(lastErrorText("CreatePipe"));
    }

    // Only the three stdio handles are inherited, even while other threads spawn processes.
    HANDLE handles[3] = {childInput.get(), childOutput.get(), childError.get()};
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<std::byte> attributeStorage(attributeSize);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize))
    {
        return unexpected(lastErrorText("InitializeProcThreadAttributeList"));
    }
    const bool updated = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                                   sizeof(handles), nullptr, nullptr) != 0;

    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childInput.get();
    startup.StartupInfo.hStdOutput = childOutput.get();
    startup.StartupInfo.hStdError = childError.get();
    startup.lpAttributeList = attributes;

    std::vector<std::string> argv;
    argv.reserve(options.arguments.size() + 1);
    argv.push_back(utf8FromPath(options.executable));
    argv.insert(argv.end(), options.arguments.begin(), options.arguments.end());
    std::wstring commandLine = widen(windowsCommandLine(argv));
    std::wstring environment;
    if (!options.environment.empty())
    {
        environment = environmentBlock(options.environment);
    }
    const std::wstring executable = options.executable.wstring();
    const std::wstring directory = options.workingDirectory.wstring();

    PROCESS_INFORMATION info = {};
    const BOOL created =
        updated &&
        CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                       environment.empty() ? nullptr : environment.data(),
                       directory.empty() ? nullptr : directory.c_str(), &startup.StartupInfo, &info);
    const std::string failure = created ? std::string() : lastErrorText("CreateProcess");
    DeleteProcThreadAttributeList(attributes);
    if (!created)
    {
        return unexpected(failure);
    }
    CloseHandle(info.hThread);
    impl->pid = info.dwProcessId;
    impl->detached = options.detached;
    if (options.detached)
    {
        CloseHandle(info.hProcess);
    }
    else
    {
        impl->process.reset(info.hProcess);
    }
    childInput.reset();
    childOutput.reset();
    childError.reset();
    for (Impl::Reader* reader : {&impl->output, &impl->error})
    {
        if (reader->captured)
        {
            impl->startReader(*reader);
        }
    }
    Process process;
    process.m_impl = std::move(impl);
    return process;
}

bool Process::writeInput(std::string_view bytes)
{
    if (!m_impl)
    {
        return false;
    }
    const std::lock_guard lock(m_impl->inputMutex);
    while (!bytes.empty())
    {
        if (m_impl->input.get() == nullptr)
        {
            return false;
        }
        DWORD written = 0;
        const DWORD size = static_cast<DWORD>(std::min<usize>(bytes.size(), 1u << 30));
        if (!WriteFile(m_impl->input.get(), bytes.data(), size, &written, nullptr))
        {
            return false;
        }
        bytes.remove_prefix(written);
    }
    return true;
}

void Process::closeInput() noexcept
{
    if (m_impl)
    {
        const std::lock_guard lock(m_impl->inputMutex);
        m_impl->input.reset();
    }
}

bool isProcessRunning(u64 pid) noexcept
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
    {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD code = 0;
    const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

u64 currentProcessId() noexcept
{
    return GetCurrentProcessId();
}

std::optional<std::filesystem::path> findExecutable(std::string_view name)
{
    const std::filesystem::path given = utf8Path(name);
    std::error_code code;
    if (given.has_parent_path())
    {
        return std::filesystem::is_regular_file(given, code) ? std::optional(given) : std::nullopt;
    }
    const auto variable = [](const wchar_t* key) {
        std::wstring value(GetEnvironmentVariableW(key, nullptr, 0), L'\0');
        const DWORD size = GetEnvironmentVariableW(key, value.data(), static_cast<DWORD>(value.size()));
        value.resize(size);
        return value;
    };
    const std::wstring path = variable(L"PATH");
    std::wstring extensions = variable(L"PATHEXT");
    if (extensions.empty())
    {
        extensions = L".COM;.EXE;.BAT;.CMD";
    }
    std::vector<std::wstring> suffixes = {L""};
    if (!given.has_extension())
    {
        for (usize begin = 0; begin <= extensions.size();)
        {
            const usize end = std::min(extensions.find(L';', begin), extensions.size());
            if (end > begin)
            {
                suffixes.push_back(extensions.substr(begin, end - begin));
            }
            begin = end + 1;
        }
    }
    for (usize begin = 0; begin <= path.size();)
    {
        const usize end = std::min(path.find(L';', begin), path.size());
        if (end > begin)
        {
            for (const std::wstring& suffix : suffixes)
            {
                std::filesystem::path candidate =
                    std::filesystem::path(path.substr(begin, end - begin)) / (given.wstring() + suffix);
                if (std::filesystem::is_regular_file(candidate, code))
                {
                    return candidate;
                }
            }
        }
        begin = end + 1;
    }
    return std::nullopt;
}

#else

void Process::Impl::startReader(Reader& reader)
{
    if (!makePipe(reader.wake))
    {
        reader.wake[0] = reader.wake[1] = -1;
    }
    reader.thread = std::thread([&reader] {
        std::string chunk(reader.buffer.chunkSize(), '\0');
        for (;;)
        {
            pollfd fds[2] = {{reader.pipe, POLLIN, 0}, {reader.wake[0], POLLIN, 0}};
            const int ready = ::poll(fds, static_cast<nfds_t>(reader.wake[0] >= 0 ? 2 : 1), -1);
            if (ready < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
            if (reader.wake[0] >= 0 && (fds[1].revents & POLLIN) != 0)
            {
                break;
            }
            const ssize_t count = ::read(reader.pipe, chunk.data(), chunk.size());
            if (count < 0 && errno == EINTR)
            {
                continue;
            }
            if (count <= 0 || !reader.buffer.append(chunk.data(), static_cast<usize>(count)))
            {
                break;
            }
        }
        reader.buffer.finish();
    });
}

void Process::Impl::stopReader(Reader& reader)
{
    if (reader.thread.joinable())
    {
        reader.buffer.shutdown();
        if (reader.wake[1] >= 0)
        {
            const char byte = 0;
            [[maybe_unused]] const ssize_t written = ::write(reader.wake[1], &byte, 1);
        }
        reader.thread.join();
    }
    closeFd(reader.pipe);
    closeFd(reader.wake[0]);
    closeFd(reader.wake[1]);
}

std::optional<i32> Process::Impl::waitFor(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(exitMutex);
    if (pid == 0)
    {
        return std::nullopt;
    }
    if (timeout == std::chrono::milliseconds::max())
    {
        exitChanged.wait(lock, [&] { return exited; });
    }
    else if (!exitChanged.wait_for(lock, timeout, [&] { return exited; }))
    {
        return std::nullopt;
    }
    return exitCode;
}

bool Process::Impl::kill() noexcept
{
    // The waiter reaps the child only under exitMutex, so the pid cannot be reused while it is held.
    const std::lock_guard lock(exitMutex);
    if (pid == 0 || exited)
    {
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), SIGKILL) == 0;
}

Expected<Process, std::string> Process::spawn(const ProcessOptions& options)
{
    std::error_code code;
    if (!std::filesystem::is_regular_file(options.executable, code))
    {
        return unexpected("executable '" + utf8FromPath(options.executable) + "' does not exist");
    }
    ignoreSigpipe();

    auto impl = std::make_unique<Impl>();
    const bool pipeInput = options.pipeInput && !options.detached;
    const bool captureOutput = options.captureOutput && !options.detached;
    const bool captureError = options.captureError && !options.detached;
    int inputPipe[2] = {-1, -1};
    if (pipeInput && !makePipe(inputPipe))
    {
        return unexpected(errnoText("pipe", errno));
    }
    impl->input = inputPipe[1];

    int outputPipe[2] = {-1, -1};
    int errorPipe[2] = {-1, -1};
    impl->output.captured = captureOutput;
    impl->error.captured = captureError;
    if ((captureOutput && !makePipe(outputPipe)) || (captureError && !makePipe(errorPipe)))
    {
        const int error = errno;
        closeFd(inputPipe[0]);
        for (int* fd : {&outputPipe[0], &outputPipe[1], &errorPipe[0], &errorPipe[1]})
        {
            closeFd(*fd);
        }
        return unexpected(errnoText("pipe", error));
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (pipeInput)
    {
        posix_spawn_file_actions_adddup2(&actions, inputPipe[0], STDIN_FILENO);
    }
    else
    {
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    if (captureOutput)
    {
        posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
    }
    else if (options.detached)
    {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    if (captureError)
    {
        posix_spawn_file_actions_adddup2(&actions, errorPipe[1], STDERR_FILENO);
    }
    else if (options.detached)
    {
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    const std::string directory = options.workingDirectory.string();
    if (!directory.empty())
    {
        posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }

    // The child starts with default signal actions (this process ignores SIGPIPE) and an empty mask.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    int flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
#if defined(ILL_PLATFORM_MACOS)
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attributes, static_cast<short>(flags));

    const std::string executable = options.executable.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const std::string& argument : options.arguments)
    {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    std::vector<std::string> environment;
    for (char** entry = currentEnvironment(); entry != nullptr && *entry != nullptr; ++entry)
    {
        const std::string_view text(*entry);
        const std::string_view name = text.substr(0, text.find('='));
        const bool replaced = std::any_of(options.environment.begin(), options.environment.end(),
                                          [&](const auto& variable) { return variable.first == name; });
        if (!replaced)
        {
            environment.emplace_back(text);
        }
    }
    for (const auto& [name, value] : options.environment)
    {
        environment.push_back(name + "=" + value);
    }
    std::vector<char*> envp;
    envp.reserve(environment.size() + 1);
    for (std::string& entry : environment)
    {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    pid_t pid = 0;
    const int spawned =
        posix_spawn(&pid, executable.c_str(), &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    closeFd(inputPipe[0]);
    closeFd(outputPipe[1]);
    closeFd(errorPipe[1]);
    if (spawned != 0)
    {
        closeFd(outputPipe[0]);
        closeFd(errorPipe[0]);
        return unexpected(errnoText("posix_spawn", spawned));
    }

    impl->pid = static_cast<u64>(pid);
    impl->output.pipe = outputPipe[0];
    impl->error.pipe = errorPipe[0];
    for (Impl::Reader* reader : {&impl->output, &impl->error})
    {
        reader->buffer.setCapacity(options.bufferCapacity);
        if (reader->captured)
        {
            impl->startReader(*reader);
        }
        else
        {
            reader->buffer.finish();
        }
    }
    if (options.detached)
    {
        // Nobody waits for a detached child; this thread reaps it so that it does not stay a zombie.
        impl->detached = true;
        std::thread([pid] {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            {
            }
        }).detach();
        Process process;
        process.m_impl = std::move(impl);
        return process;
    }
    Impl* state = impl.get();
    impl->waiter = std::thread([state, pid] {
        // Wait without reaping first, so that kill() never signals a reused pid.
        siginfo_t info = {};
        while (waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOWAIT) != 0 && errno == EINTR)
        {
        }
        const std::lock_guard lock(state->exitMutex);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        {
        }
        state->exitCode = WIFEXITED(status)     ? WEXITSTATUS(status)
                          : WIFSIGNALED(status) ? -WTERMSIG(status)
                                                : -1;
        state->exited = true;
        state->exitChanged.notify_all();
    });

    Process process;
    process.m_impl = std::move(impl);
    return process;
}

bool Process::writeInput(std::string_view bytes)
{
    if (!m_impl)
    {
        return false;
    }
    const std::lock_guard lock(m_impl->inputMutex);
    while (!bytes.empty())
    {
        if (m_impl->input < 0)
        {
            return false;
        }
        const ssize_t written = ::write(m_impl->input, bytes.data(), bytes.size());
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        bytes.remove_prefix(static_cast<usize>(written));
    }
    return true;
}

void Process::closeInput() noexcept
{
    if (m_impl)
    {
        const std::lock_guard lock(m_impl->inputMutex);
        closeFd(m_impl->input);
    }
}

bool isProcessRunning(u64 pid) noexcept
{
    return pid != 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
}

u64 currentProcessId() noexcept
{
    return static_cast<u64>(getpid());
}

std::optional<std::filesystem::path> findExecutable(std::string_view name)
{
    const std::filesystem::path given = utf8Path(name);
    if (given.has_parent_path())
    {
        return access(given.c_str(), X_OK) == 0 ? std::optional(given) : std::nullopt;
    }
    const char* variable = std::getenv("PATH");
    const std::string_view path = variable != nullptr ? variable : "/usr/bin:/bin";
    for (usize begin = 0; begin <= path.size();)
    {
        const usize end = std::min(path.find(':', begin), path.size());
        if (end > begin)
        {
            std::filesystem::path candidate = std::filesystem::path(path.substr(begin, end - begin)) / given;
            std::error_code code;
            if (std::filesystem::is_regular_file(candidate, code) && access(candidate.c_str(), X_OK) == 0)
            {
                return candidate;
            }
        }
        begin = end + 1;
    }
    return std::nullopt;
}

#endif

Process::Process() noexcept = default;
Process::Process(Process&& other) noexcept = default;
Process& Process::operator=(Process&& other) noexcept = default;
Process::~Process() = default;

u64 Process::id() const noexcept
{
    return m_impl ? m_impl->pid : 0;
}

usize Process::read(ProcessStream stream, std::string& out, usize maxBytes)
{
    return m_impl ? m_impl->reader(stream).buffer.take(out, maxBytes) : 0;
}

bool Process::readLine(ProcessStream stream, std::string& line, std::chrono::milliseconds timeout)
{
    return m_impl && m_impl->reader(stream).buffer.takeLine(line, timeout);
}

bool Process::drained(ProcessStream stream) const
{
    return !m_impl || m_impl->reader(stream).buffer.drained();
}

std::optional<i32> Process::poll()
{
    return m_impl && !m_impl->detached ? m_impl->waitFor(std::chrono::milliseconds(0)) : std::nullopt;
}

std::optional<i32> Process::wait(std::chrono::milliseconds timeout)
{
    return m_impl && !m_impl->detached ? m_impl->waitFor(timeout) : std::nullopt;
}

bool Process::terminate() noexcept
{
    return m_impl && !m_impl->detached && m_impl->kill();
}

} // namespace illusion
