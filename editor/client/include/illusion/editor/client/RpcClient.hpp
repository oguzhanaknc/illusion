#pragma once

/// @file
/// A client of the editor protocol: connects to a running editor (socket or endpoint file) or launches one
/// with `--rpc-stdio`, sends requests and reads replies and events.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>
#include <illusion/platform/Process.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::client
{

/// The error of a failed call: a JSON-RPC error object, or a transport failure (code 0).
struct RpcError
{
    i32 code = 0;        ///< JSON-RPC or ErrorKind code; 0 if the editor did not answer.
    std::string message; ///< Error message.
    std::string data{};  ///< `data` as compact JSON ("" if absent).
    std::string json{};  ///< The whole error object as compact JSON ("" for transport failures).
};

/// How to start an editor for RpcClient::launch(). Its log (stderr) goes to this process's stderr.
struct LaunchOptions
{
    std::filesystem::path editor;             ///< The `illusion_editor` executable.
    std::vector<std::string> arguments{};     ///< Its arguments; `--headless --rpc-stdio` are added.
    std::filesystem::path workingDirectory{}; ///< Directory to start it in (empty: the current one).
    /// Capture its log (stderr) for RpcClient::readLog() instead of sharing this process's stderr.
    bool captureLog = false;
    /// True for an editor: `--headless --rpc-stdio` are added and close() asks it to quit with `editor.quit`.
    /// False for other programs that speak JSON-RPC on stdio (illusion_mcp): they get only `arguments` and
    /// are closed by closing their stdin.
    bool isEditor = true;
};

/// Default time to wait for a reply.
inline constexpr std::chrono::milliseconds kDefaultCallTimeout{300000};

/// Something that runs editor methods: an RpcClient, or an McpClient that runs them as MCP tools.
class CallChannel
{
public:
    virtual ~CallChannel() = default;

    /// Calls @p method with @p params (a JSON object as text) and waits up to @p timeout for the reply.
    /// Returns the result as compact JSON.
    [[nodiscard]] virtual Expected<std::string, RpcError>
    call(std::string_view method, std::string_view params, std::chrono::milliseconds timeout) = 0;

    /// True while the connection is open.
    [[nodiscard]] virtual bool connected() const noexcept = 0;

    /// Closes the connection and ends a launched process, waiting up to @p timeout; returns its exit code.
    virtual std::optional<i32> close(std::chrono::milliseconds timeout) = 0;

protected:
    CallChannel() = default;
    CallChannel(const CallChannel&) = default;
    CallChannel& operator=(const CallChannel&) = default;
};

/// A connection to an editor. Not thread-safe: use it from one thread.
class RpcClient final : public CallChannel
{
public:
    /// Not connected.
    RpcClient() noexcept;
    /// Takes over @p other.
    RpcClient(RpcClient&& other) noexcept;
    /// Closes the current connection and takes over @p other.
    RpcClient& operator=(RpcClient&& other) noexcept;
    RpcClient(const RpcClient&) = delete;            ///< Not copyable.
    RpcClient& operator=(const RpcClient&) = delete; ///< Not copyable.
    /// Closes the connection (see close()).
    ~RpcClient() override;

    /// A client on an open @p stream that needs no authentication (stdio, in-process pairs).
    [[nodiscard]] static RpcClient overStream(std::unique_ptr<rpc::RpcStream> stream);

    /// Connects to the socket at @p socket and authenticates with @p token as @p client.
    [[nodiscard]] static Expected<RpcClient, std::string> connect(const std::filesystem::path& socket,
                                                                  std::string_view token,
                                                                  std::string_view client = "illusion_ctl");

    /// Reads the endpoint file @p endpoint (see rpc::readEndpointFile()) and connects to its editor.
    [[nodiscard]] static Expected<RpcClient, std::string>
    connectEndpoint(const std::filesystem::path& endpoint, std::string_view client = "illusion_ctl");

    /// Starts an editor with `--headless --rpc-stdio` and talks to it over its stdin and stdout.
    [[nodiscard]] static Expected<RpcClient, std::string> launch(const LaunchOptions& options);

    /// Calls @p method with @p params (a JSON object as text) and waits up to @p timeout for the reply.
    /// Returns the result as compact JSON.
    [[nodiscard]] Expected<std::string, RpcError>
    call(std::string_view method, std::string_view params = "{}",
         std::chrono::milliseconds timeout = kDefaultCallTimeout) override;

    /// Sends a request for @p method without waiting; returns its id (nothing if the connection is closed).
    /// Collect the reply with poll().
    [[nodiscard]] std::optional<u64> start(std::string_view method, std::string_view params = "{}");

    /// Waits up to @p timeout for the reply to request @p id (see start()); notifications that arrive
    /// meanwhile are kept for nextNotification(). Returns nothing on a timeout, the result as compact JSON,
    /// or the error (code 0 if the connection closed).
    [[nodiscard]] std::optional<Expected<std::string, RpcError>> poll(u64 id,
                                                                      std::chrono::milliseconds timeout);

    /// Sends a notification (no reply). Returns false if the connection is closed.
    bool notify(std::string_view method, std::string_view params = "{}");

    /// The next notification from the editor (a whole message as compact JSON), waiting up to @p timeout.
    /// Replies to requests read meanwhile are dropped: do not use it while a request started with start() is
    /// outstanding (use takeNotification()).
    [[nodiscard]] std::optional<std::string> nextNotification(std::chrono::milliseconds timeout);

    /// The next notification that has already been read (by poll() or call()), without reading more.
    [[nodiscard]] std::optional<std::string> takeNotification();

    /// True while the connection is open.
    [[nodiscard]] bool connected() const noexcept override { return m_stream != nullptr && !m_closed; }

    /// The launched editor process (nullptr unless launch() created this client).
    [[nodiscard]] Process* process() noexcept { return m_process.get(); }

    /// The next line the launched editor logged, if LaunchOptions::captureLog was set and one is waiting.
    [[nodiscard]] std::optional<std::string> readLog();

    /// Closes the connection. A launched editor is asked to quit (`editor.quit`) and waited for up to
    /// @p timeout, then terminated. Returns its exit code (nothing for other connections).
    std::optional<i32> close(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) override;

private:
    // Reads until a message arrives or @p timeout passes; false if the connection closed or timed out.
    bool readMessage(std::string& message, std::chrono::milliseconds timeout);

    std::unique_ptr<rpc::RpcStream> m_stream;
    std::unique_ptr<Process> m_process;
    rpc::LineFramer m_framer;
    bool m_editor = true;
    std::deque<std::string> m_notifications;
    std::deque<std::string> m_lines;
    u64 m_nextId = 1;
    bool m_closed = false;
};

} // namespace illusion::editor::client
