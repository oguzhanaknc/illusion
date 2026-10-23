#pragma once

/// @file
/// The editor's JSON-RPC server: connections on I/O threads, requests run on the main thread (ADR 0037).

#include <illusion/core/Base.hpp>
#include <illusion/editor/core/CommandRegistry.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace illusion::editor
{
class EditorContext;
struct EditorEvent;
} // namespace illusion::editor

namespace illusion::editor::rpc
{

struct Request;

/// What RPC clients may run.
enum class PermissionMode : u8
{
    ReadOnly, ///< Only read-only commands.
    Ask,      ///< Destructive, files, build and system commands need a person's approval (denied headless).
    Auto,     ///< Everything (the headless default).
};

/// Stable name of @p mode: "read-only", "ask" or "auto".
[[nodiscard]] std::string_view permissionModeName(PermissionMode mode) noexcept;

/// The mode named @p name (see permissionModeName()).
[[nodiscard]] std::optional<PermissionMode> parsePermissionMode(std::string_view name) noexcept;

/// True if @p mode lets RPC clients run a command of class @p permission. @p readOnly is the command's flag;
/// @p interactive is true when a person can approve requests (never in M7j, which has no editor window).
[[nodiscard]] bool permits(PermissionMode mode, PermissionClass permission, bool readOnly,
                           bool interactive = false) noexcept;

/// Settings of an RpcServer.
struct RpcServerOptions
{
    PermissionMode permissions = PermissionMode::Auto;     ///< What clients may run.
    usize maxConnections = 8;                              ///< Open connections at most.
    usize maxInFlight = 64;                                ///< Queued requests per connection at most.
    usize queueCapacity = 256;                             ///< Queued requests of all connections at most.
    usize eventQueueCapacity = 1024;                       ///< Events kept for `editor.poll_events`.
    usize idempotencyTokens = 1024;                        ///< `clientToken`s remembered per client.
    std::chrono::milliseconds authenticationTimeout{2000}; ///< Time a socket client has for rpc.authenticate.
    std::string editorVersion{};                           ///< Reported to clients.
    /// Called from I/O threads after a request was queued (the windowed editor wakes its event loop).
    std::function<void()> wake{};
};

/// Serves the editor protocol.
///
/// I/O threads read and frame messages, answer protocol errors, authentication, `rpc.discover`, unknown
/// methods and permission denials themselves, and queue the rest. The editor's main thread runs the queue
/// with pump(), between frames, so commands never race with the World. Requests of one connection run and
/// are answered in order. Besides the commands of the registry the server implements `editor.subscribe`,
/// `editor.unsubscribe`, `editor.poll_events` and `editor.quit`. The method catalogue is taken when the
/// server is created.
class RpcServer
{
public:
    /// A server for @p editor, which must outlive it. Call it on the editor's main thread.
    explicit RpcServer(EditorContext& editor, RpcServerOptions options = {});
    /// Stops the server (see stop()).
    ~RpcServer();
    RpcServer(const RpcServer&) = delete;            ///< Not copyable.
    RpcServer& operator=(const RpcServer&) = delete; ///< Not copyable.

    /// Serves @p stream as a connection. @p trusted connections need no rpc.authenticate (stdio: the parent
    /// process owns the pipes); @p client names it in origins (`rpc:<client>`). Returns the connection id, or
    /// 0 (and closes the stream) when maxConnections are open.
    u32 addConnection(std::unique_ptr<RpcStream> stream, bool trusted, std::string_view client = {});

    /// Accepts clients on @p listener from a thread of the server. They must authenticate with token().
    void listen(SocketListener listener);

    /// The token socket clients pass to rpc.authenticate (64 hex digits).
    [[nodiscard]] const std::string& token() const noexcept { return m_token; }

    /// Runs queued requests on the calling (main) thread until the queue is empty or @p budget has passed.
    /// Returns how many ran.
    usize pump(std::chrono::microseconds budget = std::chrono::microseconds::max());

    /// Waits up to @p timeout until a request is queued (for loops without an event loop). True if one is.
    bool waitForWork(std::chrono::milliseconds timeout);

    /// True once a client called `editor.quit`.
    [[nodiscard]] bool quitRequested() const noexcept { return m_quit; }

    /// True while connection @p id is open.
    [[nodiscard]] bool isOpen(u32 id) const;

    /// Number of open connections.
    [[nodiscard]] usize connectionCount() const;

    /// The OpenRPC 1.3 document that `rpc.discover` returns (compact JSON).
    [[nodiscard]] const std::string& discoverDocument() const noexcept { return m_discover; }

    /// Stops listening, closes every connection and drops queued requests. Called by the destructor.
    void stop();

private:
    struct Connection;
    struct Shared;

    std::string execute(Connection& connection, const Request& request);
    void deliver(const EditorEvent& event);
    void reap();

    EditorContext& m_editor;
    std::string m_token;
    std::string m_discover;
    bool m_quit = false;
    u32 m_subscription = 0;
    std::shared_ptr<Shared> m_shared;
};

} // namespace illusion::editor::rpc
