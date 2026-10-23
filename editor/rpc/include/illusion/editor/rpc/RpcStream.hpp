#pragma once

/// @file
/// Byte streams that carry the editor protocol: stdio, `AF_UNIX` sockets and in-memory pairs, plus the
/// endpoint file through which clients find a running editor (ADR 0037).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace illusion::editor::rpc
{

/// What RpcStream::read() returned.
enum class ReadStatus : u8
{
    Data,    ///< Bytes were read.
    Timeout, ///< Nothing arrived in time.
    Closed,  ///< The peer closed the stream, or close() was called.
};

/// A bidirectional byte stream. One thread reads and one thread writes at a time; close() may be called from
/// any thread and makes a blocked read() return Closed.
class RpcStream
{
public:
    virtual ~RpcStream() = default;

    /// Reads up to `buffer.size()` bytes into @p buffer, waiting up to @p timeout
    /// (`std::chrono::milliseconds::max()` waits forever). @p count receives the number of bytes read.
    virtual ReadStatus read(std::span<char> buffer, usize& count, std::chrono::milliseconds timeout) = 0;

    /// Writes all of @p bytes; returns false if the stream is closed or the peer is gone.
    virtual bool write(std::string_view bytes) = 0;

    /// Closes the stream: reads return Closed and writes fail from now on.
    virtual void close() noexcept = 0;

protected:
    RpcStream() = default;
    RpcStream(const RpcStream&) = delete;
    RpcStream& operator=(const RpcStream&) = delete;
};

/// Two connected in-memory streams: what one writes, the other reads (tests, benchmarks, in-process clients).
/// Closing one makes the other read end of stream.
[[nodiscard]] std::pair<std::unique_ptr<RpcStream>, std::unique_ptr<RpcStream>> makeStreamPair();

/// A stream over this process's stdin (read) and stdout (write). Nothing else may use them while it lives;
/// log to stderr (ConsoleStreams::StderrOnly).
[[nodiscard]] std::unique_ptr<RpcStream> makeStdioStream();

/// A listening `AF_UNIX` socket.
class SocketListener
{
public:
    /// Not listening.
    SocketListener() noexcept;
    /// Takes over @p other.
    SocketListener(SocketListener&& other) noexcept;
    /// Closes the current socket and takes over @p other.
    SocketListener& operator=(SocketListener&& other) noexcept;
    SocketListener(const SocketListener&) = delete;            ///< Not copyable.
    SocketListener& operator=(const SocketListener&) = delete; ///< Not copyable.
    /// Stops listening and deletes the socket file.
    ~SocketListener();

    /// Listens at @p path, replacing a stale socket file; the file is made accessible to the current user
    /// only. Fails if @p path is too long for a socket address (see defaultSocketPath()).
    [[nodiscard]] static Expected<SocketListener, std::string> listen(const std::filesystem::path& path);

    /// Waits up to @p timeout for a client; nullptr on timeout or after close().
    [[nodiscard]] std::unique_ptr<RpcStream> accept(std::chrono::milliseconds timeout);

    /// Stops listening; a blocked accept() returns nullptr. May be called from any thread.
    void close() noexcept;

    /// The socket file.
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Connects to the `AF_UNIX` socket at @p path.
[[nodiscard]] Expected<std::unique_ptr<RpcStream>, std::string>
connectSocket(const std::filesystem::path& path);

/// Longest socket path in bytes that every platform accepts (`sun_path` is 104 bytes on macOS, including
/// the terminator).
inline constexpr usize kMaxSocketPath = 103;

/// Where the editor of the project in @p projectRoot listens: `<project>/.illusion/run/editor.sock`, or
/// `illusion-<hash>.sock` in the per-user runtime directory (`XDG_RUNTIME_DIR`, `TMPDIR` or the temp
/// directory) when that path is too long or, on Windows, not ASCII.
[[nodiscard]] std::filesystem::path defaultSocketPath(const std::filesystem::path& projectRoot);

/// Makes @p path (a file or directory) accessible to the current user only: mode 0600 or 0700 on POSIX
/// systems, a protected access list granting only the current user (inherited by new files in a directory)
/// on Windows.
[[nodiscard]] bool restrictToOwner(const std::filesystem::path& path);

/// True if only the current user may access @p path (see restrictToOwner()).
[[nodiscard]] bool isOwnerOnly(const std::filesystem::path& path);

/// What a running editor writes to its endpoint file.
struct EndpointInfo
{
    u64 pid = 0;                 ///< Process id of the editor.
    std::string protocolVersion; ///< kProtocolVersion of the editor.
    std::string editorVersion;   ///< Engine version of the editor.
    std::string transport;       ///< "unix".
    std::string address;         ///< Socket path (UTF-8).
    std::string token;           ///< Authentication token (64 hex digits).
};

/// `<project>/.illusion/run/editor-endpoint.json`. An editor that listens on a socket without a project
/// writes `<socket>.endpoint.json` instead.
[[nodiscard]] std::filesystem::path endpointFilePath(const std::filesystem::path& projectRoot);

/// Writes @p info to @p file atomically (a temporary file renamed over it), readable by the current user
/// only. A missing directory is created and restricted to the current user; an existing one is left as it is.
[[nodiscard]] Expected<void, std::string> writeEndpointFile(const std::filesystem::path& file,
                                                            const EndpointInfo& info);

/// Reads an endpoint file. Fails if it is missing, malformed or stale (its editor process is gone).
[[nodiscard]] Expected<EndpointInfo, std::string> readEndpointFile(const std::filesystem::path& file);

/// A new authentication token: 256 bits from secureRandomBytes() as 64 lower-case hex digits ("" if the
/// system has no secure random source).
[[nodiscard]] std::string makeToken();

} // namespace illusion::editor::rpc
