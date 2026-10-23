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
};

/// Default time to wait for a reply.
inline constexpr std::chrono::milliseconds kDefaultCallTimeout{300000};

/// A connection to an editor. Not thread-safe: use it from one thread.
class RpcClient
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
    ~RpcClient();

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
         std::chrono::milliseconds timeout = kDefaultCallTimeout);

    /// Sends a notification (no reply). Returns false if the connection is closed.
    bool notify(std::string_view method, std::string_view params = "{}");

    /// The next notification from the editor (a whole message as compact JSON), waiting up to @p timeout.
    [[nodiscard]] std::optional<std::string> nextNotification(std::chrono::milliseconds timeout);

    /// True while the connection is open.
    [[nodiscard]] bool connected() const noexcept { return m_stream != nullptr && !m_closed; }

    /// The launched editor process (nullptr unless launch() created this client).
    [[nodiscard]] Process* process() noexcept { return m_process.get(); }

    /// Closes the connection. A launched editor is asked to quit (`editor.quit`) and waited for up to
    /// @p timeout, then terminated. Returns its exit code (nothing for other connections).
    std::optional<i32> close(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

private:
    // Reads until a message arrives or @p timeout passes; false if the connection closed or timed out.
    bool readMessage(std::string& message, std::chrono::milliseconds timeout);

    std::unique_ptr<rpc::RpcStream> m_stream;
    std::unique_ptr<Process> m_process;
    rpc::LineFramer m_framer;
    std::deque<std::string> m_notifications;
    std::deque<std::string> m_lines;
    u64 m_nextId = 1;
    bool m_closed = false;
};

} // namespace illusion::editor::client
