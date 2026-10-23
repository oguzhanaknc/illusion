#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(ILL_PLATFORM_WINDOWS)
// clang-format off
#include <winsock2.h>
#include <afunix.h>
#include <aclapi.h>
#include <windows.h>
// clang-format on
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace illusion::editor::rpc
{
namespace
{

// Blocking waits are cut into slices so that close() from another thread takes effect promptly.
constexpr std::chrono::milliseconds kSlice{50};

std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// Remaining wait of a timeout that started at @p start; max() stays max().
std::chrono::milliseconds remaining(std::chrono::milliseconds timeout,
                                    std::chrono::steady_clock::time_point start)
{
    if (timeout == std::chrono::milliseconds::max())
    {
        return timeout;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return elapsed >= timeout ? std::chrono::milliseconds(0) : timeout - elapsed;
}

// In-memory streams.

struct Channel
{
    std::mutex mutex;
    std::condition_variable readable;
    std::string data;
    bool closed = false;
};

class MemoryStream final : public RpcStream
{
public:
    MemoryStream(std::shared_ptr<Channel> in, std::shared_ptr<Channel> out)
        : m_in(std::move(in))
        , m_out(std::move(out))
    {
    }
    ~MemoryStream() override { close(); }

    ReadStatus read(std::span<char> buffer, usize& count, std::chrono::milliseconds timeout) override
    {
        count = 0;
        std::unique_lock lock(m_in->mutex);
        const auto ready = [&] {
            return !m_in->data.empty() || m_in->closed;
        };
        if (timeout == std::chrono::milliseconds::max())
        {
            m_in->readable.wait(lock, ready);
        }
        else if (!m_in->readable.wait_for(lock, timeout, ready))
        {
            return ReadStatus::Timeout;
        }
        if (m_in->data.empty())
        {
            return ReadStatus::Closed;
        }
        count = std::min(buffer.size(), m_in->data.size());
        std::memcpy(buffer.data(), m_in->data.data(), count);
        m_in->data.erase(0, count);
        return ReadStatus::Data;
    }

    bool write(std::string_view bytes) override
    {
        const std::lock_guard lock(m_out->mutex);
        if (m_out->closed)
        {
            return false;
        }
        m_out->data.append(bytes);
        m_out->readable.notify_all();
        return true;
    }

    void close() noexcept override
    {
        for (Channel* channel : {m_in.get(), m_out.get()})
        {
            const std::lock_guard lock(channel->mutex);
            channel->closed = true;
            channel->readable.notify_all();
        }
        // This end drops what it has not read; the peer still reads what was written before.
        const std::lock_guard lock(m_in->mutex);
        m_in->data.clear();
    }

private:
    std::shared_ptr<Channel> m_in;
    std::shared_ptr<Channel> m_out;
};

#if defined(ILL_PLATFORM_WINDOWS)

using SocketHandle = SOCKET;
using IoLength = int; // the length type of recv() and send()
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

bool startSockets()
{
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}

void closeSocket(SocketHandle socket) noexcept
{
    closesocket(socket);
}

int pollOne(SocketHandle socket, std::chrono::milliseconds wait)
{
    WSAPOLLFD entry = {socket, POLLRDNORM, 0};
    return WSAPoll(&entry, 1, static_cast<INT>(wait.count()));
}

std::string socketError(std::string_view what)
{
    return std::string(what) + " failed (WSA error " + std::to_string(WSAGetLastError()) + ")";
}

void configureSocket(SocketHandle) noexcept
{
}

constexpr int kSendFlags = 0;

#else

using SocketHandle = int;
using IoLength = usize; // the length type of recv() and send()
constexpr SocketHandle kInvalidSocket = -1;

bool startSockets()
{
    return true;
}

void closeSocket(SocketHandle socket) noexcept
{
    ::close(socket);
}

int pollOne(SocketHandle socket, std::chrono::milliseconds wait)
{
    pollfd entry = {socket, POLLIN, 0};
    return ::poll(&entry, 1, static_cast<int>(wait.count()));
}

std::string socketError(std::string_view what)
{
    return std::string(what) + " failed: " + std::strerror(errno);
}

void configureSocket(SocketHandle socket) noexcept
{
    fcntl(socket, F_SETFD, FD_CLOEXEC);
#if defined(ILL_PLATFORM_MACOS)
    const int on = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

#if defined(ILL_PLATFORM_MACOS)
constexpr int kSendFlags = 0;
#else
constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

#endif

// Waits in slices until @p socket is readable, @p closed is set or @p timeout passes. Returns 1 if readable,
// 0 on timeout and -1 if closed or on error.
int waitReadable(SocketHandle socket, const std::atomic<bool>& closed, std::chrono::milliseconds timeout)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        if (closed.load())
        {
            return -1;
        }
        const std::chrono::milliseconds left = remaining(timeout, start);
        const int ready = pollOne(socket, std::min(left, kSlice));
        if (ready > 0)
        {
            return closed.load() ? -1 : 1;
        }
#if !defined(ILL_PLATFORM_WINDOWS)
        if (ready < 0 && errno != EINTR)
        {
            return -1;
        }
#else
        if (ready < 0)
        {
            return -1;
        }
#endif
        if (left.count() == 0)
        {
            return 0;
        }
    }
}

class SocketStream final : public RpcStream
{
public:
    explicit SocketStream(SocketHandle socket) noexcept
        : m_socket(socket)
    {
        configureSocket(socket);
    }
    ~SocketStream() override
    {
        close();
        closeSocket(m_socket);
    }

    ReadStatus read(std::span<char> buffer, usize& count, std::chrono::milliseconds timeout) override
    {
        count = 0;
        for (;;)
        {
            const int ready = waitReadable(m_socket, m_closed, timeout);
            if (ready == 0)
            {
                return ReadStatus::Timeout;
            }
            if (ready < 0)
            {
                return ReadStatus::Closed;
            }
            const auto received = ::recv(m_socket, buffer.data(),
                                         static_cast<IoLength>(std::min<usize>(buffer.size(), 1u << 30)), 0);
            if (received > 0)
            {
                count = static_cast<usize>(received);
                return ReadStatus::Data;
            }
#if !defined(ILL_PLATFORM_WINDOWS)
            if (received < 0 && (errno == EINTR || errno == EAGAIN))
            {
                continue;
            }
#endif
            return ReadStatus::Closed;
        }
    }

    bool write(std::string_view bytes) override
    {
        while (!bytes.empty())
        {
            if (m_closed.load())
            {
                return false;
            }
            const auto sent =
                ::send(m_socket, bytes.data(), static_cast<IoLength>(std::min<usize>(bytes.size(), 1u << 30)),
                       kSendFlags);
            if (sent <= 0)
            {
#if !defined(ILL_PLATFORM_WINDOWS)
                if (sent < 0 && errno == EINTR)
                {
                    continue;
                }
#endif
                return false;
            }
            bytes.remove_prefix(static_cast<usize>(sent));
        }
        return true;
    }

    void close() noexcept override
    {
        if (!m_closed.exchange(true))
        {
#if defined(ILL_PLATFORM_WINDOWS)
            shutdown(m_socket, SD_BOTH);
#else
            shutdown(m_socket, SHUT_RDWR);
#endif
        }
    }

private:
    SocketHandle m_socket;
    std::atomic<bool> m_closed{false};
};

bool fillAddress(const std::filesystem::path& path, sockaddr_un& address, std::string& error)
{
    const std::string text = utf8Of(path);
    if (text.empty() || text.size() > kMaxSocketPath)
    {
        error =
            "socket path '" + text + "' is empty or longer than " + std::to_string(kMaxSocketPath) + " bytes";
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, text.data(), text.size());
    return true;
}

// stdio.

#if defined(ILL_PLATFORM_WINDOWS)

class StdioStream final : public RpcStream
{
public:
    StdioStream()
        : m_input(GetStdHandle(STD_INPUT_HANDLE))
        , m_output(GetStdHandle(STD_OUTPUT_HANDLE))
    {
    }
    ~StdioStream() override
    {
        close();
        if (m_reader != nullptr)
        {
            CloseHandle(m_reader);
        }
    }

    ReadStatus read(std::span<char> buffer, usize& count, std::chrono::milliseconds) override
    {
        count = 0;
        if (m_reader == nullptr)
        {
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m_reader, 0, FALSE,
                            DUPLICATE_SAME_ACCESS);
        }
        // close() cancels the ReadFile below until m_reading is false; seq_cst ordering of the two flags
        // makes sure it either sees this read or this read sees m_closed.
        m_reading.store(true);
        if (m_closed.load())
        {
            m_reading.store(false);
            return ReadStatus::Closed;
        }
        DWORD read = 0;
        const BOOL ok =
            ReadFile(m_input, buffer.data(), static_cast<DWORD>(std::min<usize>(buffer.size(), 1u << 30)),
                     &read, nullptr);
        m_reading.store(false);
        if (!ok || read == 0 || m_closed.load())
        {
            return ReadStatus::Closed;
        }
        count = read;
        return ReadStatus::Data;
    }

    bool write(std::string_view bytes) override
    {
        while (!bytes.empty())
        {
            DWORD written = 0;
            if (m_closed.load() ||
                !WriteFile(m_output, bytes.data(),
                           static_cast<DWORD>(std::min<usize>(bytes.size(), 1u << 30)), &written, nullptr))
            {
                return false;
            }
            bytes.remove_prefix(written);
        }
        return true;
    }

    void close() noexcept override
    {
        m_closed.store(true);
        while (m_reading.load())
        {
            if (m_reader != nullptr)
            {
                CancelSynchronousIo(m_reader);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    HANDLE m_input;
    HANDLE m_output;
    HANDLE m_reader = nullptr; // the thread that reads, for CancelSynchronousIo()
    std::atomic<bool> m_reading{false};
    std::atomic<bool> m_closed{false};
};

#else

class StdioStream final : public RpcStream
{
public:
    ~StdioStream() override { close(); }

    ReadStatus read(std::span<char> buffer, usize& count, std::chrono::milliseconds timeout) override
    {
        count = 0;
        const auto start = std::chrono::steady_clock::now();
        for (;;)
        {
            if (m_closed.load())
            {
                return ReadStatus::Closed;
            }
            const std::chrono::milliseconds left = remaining(timeout, start);
            pollfd entry = {STDIN_FILENO, POLLIN, 0};
            const int ready = ::poll(&entry, 1, static_cast<int>(std::min(left, kSlice).count()));
            if (ready < 0 && errno != EINTR)
            {
                return ReadStatus::Closed;
            }
            if (ready > 0)
            {
                const ssize_t received = ::read(STDIN_FILENO, buffer.data(), buffer.size());
                if (received < 0 && errno == EINTR)
                {
                    continue;
                }
                if (received <= 0)
                {
                    return ReadStatus::Closed;
                }
                count = static_cast<usize>(received);
                return ReadStatus::Data;
            }
            if (left.count() == 0)
            {
                return ReadStatus::Timeout;
            }
        }
    }

    bool write(std::string_view bytes) override
    {
        while (!bytes.empty())
        {
            if (m_closed.load())
            {
                return false;
            }
            const ssize_t written = ::write(STDOUT_FILENO, bytes.data(), bytes.size());
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            if (written <= 0)
            {
                return false;
            }
            bytes.remove_prefix(static_cast<usize>(written));
        }
        return true;
    }

    void close() noexcept override { m_closed.store(true); }

private:
    std::atomic<bool> m_closed{false};
};

#endif

} // namespace

std::pair<std::unique_ptr<RpcStream>, std::unique_ptr<RpcStream>> makeStreamPair()
{
    auto a = std::make_shared<Channel>();
    auto b = std::make_shared<Channel>();
    return {std::make_unique<MemoryStream>(a, b), std::make_unique<MemoryStream>(b, a)};
}

std::unique_ptr<RpcStream> makeStdioStream()
{
    return std::make_unique<StdioStream>();
}

struct SocketListener::Impl
{
    SocketHandle socket = kInvalidSocket;
    std::filesystem::path path;
    std::atomic<bool> closed{false};

    ~Impl()
    {
        if (socket != kInvalidSocket)
        {
            closeSocket(socket);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};

SocketListener::SocketListener() noexcept = default;
SocketListener::SocketListener(SocketListener&& other) noexcept = default;
SocketListener& SocketListener::operator=(SocketListener&& other) noexcept = default;
SocketListener::~SocketListener() = default;

Expected<SocketListener, std::string> SocketListener::listen(const std::filesystem::path& path)
{
    if (!startSockets())
    {
        return unexpected(std::string("sockets are not available"));
    }
    sockaddr_un address;
    std::string error;
    if (!fillAddress(path, address, error))
    {
        return unexpected(std::move(error));
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    auto impl = std::make_unique<Impl>();
    impl->socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (impl->socket == kInvalidSocket)
    {
        return unexpected(socketError("socket"));
    }
    configureSocket(impl->socket);
    impl->path = path;
    if (::bind(impl->socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        std::string message = socketError("bind");
        closeSocket(impl->socket);
        impl->socket = kInvalidSocket;
        return unexpected(std::move(message));
    }
    if (!restrictToOwner(path))
    {
        return unexpected("cannot restrict '" + utf8Of(path) + "' to the current user");
    }
    if (::listen(impl->socket, 16) != 0)
    {
        return unexpected(socketError("listen"));
    }
    SocketListener listener;
    listener.m_impl = std::move(impl);
    return listener;
}

std::unique_ptr<RpcStream> SocketListener::accept(std::chrono::milliseconds timeout)
{
    if (!m_impl || waitReadable(m_impl->socket, m_impl->closed, timeout) <= 0)
    {
        return nullptr;
    }
    const SocketHandle client = ::accept(m_impl->socket, nullptr, nullptr);
    if (client == kInvalidSocket)
    {
        return nullptr;
    }
    return std::make_unique<SocketStream>(client);
}

void SocketListener::close() noexcept
{
    if (m_impl)
    {
        m_impl->closed.store(true);
    }
}

const std::filesystem::path& SocketListener::path() const noexcept
{
    static const std::filesystem::path none;
    return m_impl ? m_impl->path : none;
}

Expected<std::unique_ptr<RpcStream>, std::string> connectSocket(const std::filesystem::path& path)
{
    if (!startSockets())
    {
        return unexpected(std::string("sockets are not available"));
    }
    sockaddr_un address;
    std::string error;
    if (!fillAddress(path, address, error))
    {
        return unexpected(std::move(error));
    }
    const SocketHandle socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket == kInvalidSocket)
    {
        return unexpected(socketError("socket"));
    }
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        std::string message = socketError("connect to '" + utf8Of(path) + "'");
        closeSocket(socket);
        return unexpected(std::move(message));
    }
    return std::unique_ptr<RpcStream>(std::make_unique<SocketStream>(socket));
}

std::filesystem::path defaultSocketPath(const std::filesystem::path& projectRoot)
{
    const std::filesystem::path inProject = projectRoot / ".illusion" / "run" / "editor.sock";
    const std::string text = utf8Of(inProject);
    bool usable = text.size() <= kMaxSocketPath;
#if defined(ILL_PLATFORM_WINDOWS)
    usable = usable && std::all_of(text.begin(), text.end(),
                                   [](char c) { return static_cast<unsigned char>(c) < 0x80; });
#endif
    if (usable)
    {
        return inProject;
    }
    std::filesystem::path directory;
#if !defined(ILL_PLATFORM_WINDOWS)
    for (const char* variable : {"XDG_RUNTIME_DIR", "TMPDIR"})
    {
        if (const char* value = std::getenv(variable); value != nullptr && *value != '\0')
        {
            directory = value;
            break;
        }
    }
#endif
    if (directory.empty())
    {
        std::error_code code;
        directory = std::filesystem::temp_directory_path(code);
    }
    const std::string name = std::format("illusion-{:016x}.sock", fnv1a64(utf8Of(projectRoot)));
    return directory / name;
}

#if defined(ILL_PLATFORM_WINDOWS)

namespace
{

// The SID of the current user, in @p storage.
PSID currentUser(std::vector<std::byte>& storage)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return nullptr;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    storage.resize(size);
    const bool ok = GetTokenInformation(token, TokenUser, storage.data(), size, &size) != 0;
    CloseHandle(token);
    return ok ? reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid : nullptr;
}

} // namespace

bool restrictToOwner(const std::filesystem::path& path)
{
    std::vector<std::byte> storage;
    PSID user = currentUser(storage);
    if (user == nullptr)
    {
        return false;
    }
    std::error_code code;
    EXPLICIT_ACCESS_W access = {};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance =
        std::filesystem::is_directory(path, code) ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(user);
    PACL list = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &list) != ERROR_SUCCESS)
    {
        return false;
    }
    std::wstring name = path.wstring();
    const DWORD result = SetNamedSecurityInfoW(
        name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
        nullptr, list, nullptr);
    LocalFree(list);
    return result == ERROR_SUCCESS;
}

bool isOwnerOnly(const std::filesystem::path& path)
{
    std::vector<std::byte> storage;
    PSID user = currentUser(storage);
    PACL list = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (user == nullptr ||
        GetNamedSecurityInfoW(path.wstring().c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                              nullptr, &list, nullptr, &descriptor) != ERROR_SUCCESS)
    {
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool only = list != nullptr && GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                (control & SE_DACL_PROTECTED) != 0;
    for (DWORD i = 0; only && i < list->AceCount; ++i)
    {
        void* entry = nullptr;
        only = GetAce(list, i, &entry) != 0;
        if (only)
        {
            const auto* header = static_cast<const ACE_HEADER*>(entry);
            only = header->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                   EqualSid(&static_cast<ACCESS_ALLOWED_ACE*>(entry)->SidStart, user);
        }
    }
    LocalFree(descriptor);
    return only;
}

#else

bool restrictToOwner(const std::filesystem::path& path)
{
    struct stat info = {};
    if (::stat(path.c_str(), &info) != 0)
    {
        return false;
    }
    const mode_t mode = S_ISDIR(info.st_mode) ? mode_t{0700} : mode_t{0600};
    return ::chmod(path.c_str(), mode) == 0;
}

bool isOwnerOnly(const std::filesystem::path& path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && info.st_uid == ::getuid() && (info.st_mode & 077) == 0;
}

#endif

std::filesystem::path endpointFilePath(const std::filesystem::path& projectRoot)
{
    return projectRoot / ".illusion" / "run" / "editor-endpoint.json";
}

Expected<void, std::string> writeEndpointFile(const std::filesystem::path& file, const EndpointInfo& info)
{
    std::error_code code;
    if (!std::filesystem::exists(file.parent_path(), code))
    {
        std::filesystem::create_directories(file.parent_path(), code);
        if (!restrictToOwner(file.parent_path()))
        {
            return unexpected("cannot restrict '" + utf8Of(file.parent_path()) + "' to the current user");
        }
    }
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("pid", doc.makeU64(info.pid));
    root.add("protocolVersion", doc.makeString(info.protocolVersion));
    root.add("editorVersion", doc.makeString(info.editorVersion));
    root.add("transport", doc.makeString(info.transport));
    root.add("address", doc.makeString(info.address));
    root.add("token", doc.makeString(info.token));
    doc.setRoot(root);
    std::string text;
    if (!doc.write(text))
    {
        return unexpected(std::string("cannot write the endpoint JSON"));
    }
    text += '\n';

    const std::filesystem::path temporary =
        file.parent_path() / utf8Path(std::format(".{}.{}.tmp", utf8Of(file.filename()), currentProcessId()));
#if defined(ILL_PLATFORM_WINDOWS)
    // Restrict the empty file before the token goes into it.
    if (!std::ofstream(temporary, std::ios::binary | std::ios::trunc) || !restrictToOwner(temporary))
    {
        std::filesystem::remove(temporary, code);
        return unexpected("cannot create '" + utf8Of(temporary) + "' for the current user only");
    }
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out)
        {
            return unexpected("cannot write '" + utf8Of(temporary) + "'");
        }
    }
#else
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        return unexpected("cannot create '" + utf8Of(temporary) + "': " + std::strerror(errno));
    }
    const bool written = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    ::close(fd);
    if (!written)
    {
        return unexpected("cannot write '" + utf8Of(temporary) + "'");
    }
#endif
    if (!restrictToOwner(temporary))
    {
        std::filesystem::remove(temporary, code);
        return unexpected("cannot restrict '" + utf8Of(temporary) + "' to the current user");
    }
    std::filesystem::rename(temporary, file, code);
    if (code)
    {
        std::filesystem::remove(temporary, code);
        return unexpected("cannot replace '" + utf8Of(file) + "'");
    }
    return {};
}

Expected<EndpointInfo, std::string> readEndpointFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return unexpected("no endpoint file at '" + utf8Of(file) +
                          "' (is the editor running with --rpc-listen?)");
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(text);
    if (!doc || !doc->root().isObject())
    {
        return unexpected("'" + utf8Of(file) + "' is not a valid endpoint file");
    }
    const JsonValue root = doc->root();
    EndpointInfo info;
    info.pid = root.find("pid").asU64().value_or(0);
    info.protocolVersion = std::string(root.find("protocolVersion").asString().value_or(""));
    info.editorVersion = std::string(root.find("editorVersion").asString().value_or(""));
    info.transport = std::string(root.find("transport").asString().value_or(""));
    info.address = std::string(root.find("address").asString().value_or(""));
    info.token = std::string(root.find("token").asString().value_or(""));
    if (info.pid == 0 || info.address.empty() || info.token.empty())
    {
        return unexpected("'" + utf8Of(file) + "' is not a valid endpoint file");
    }
    if (!isProcessRunning(info.pid))
    {
        return unexpected("'" + utf8Of(file) + "' is stale: editor process " + std::to_string(info.pid) +
                          " is not running");
    }
    return info;
}

std::string makeToken()
{
    std::array<std::byte, 32> bytes{};
    if (!secureRandomBytes(bytes))
    {
        return {};
    }
    std::string token;
    token.reserve(64);
    for (const std::byte byte : bytes)
    {
        token += std::format("{:02x}", static_cast<unsigned>(byte));
    }
    return token;
}

} // namespace illusion::editor::rpc
