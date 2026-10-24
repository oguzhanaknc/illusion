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
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
/// @p interactive is true when a person can approve requests (the windowed editor).
[[nodiscard]] bool permits(PermissionMode mode, PermissionClass permission, bool readOnly,
                           bool interactive = false) noexcept;

/// True if a command of class @p permission needs a person's approval in PermissionMode::Ask.
[[nodiscard]] bool needsApproval(PermissionClass permission, bool readOnly) noexcept;

/// What a person decided about a request that waits for approval.
enum class ApprovalDecision : u8
{
    AllowOnce,       ///< Run this request.
    AllowForSession, ///< Run it, and allow its permission classes for this connection from now on.
    Deny,            ///< Answer it with PermissionDenied.
};

/// A request that waits for a person's approval (PermissionMode::Ask with RpcServerOptions::interactive).
struct PendingApproval
{
    u64 id = 0;                                         ///< Pass to RpcServer::decide().
    u32 connection = 0;                                 ///< The connection that sent it.
    std::string client;                                 ///< Its client name.
    std::string method;                                 ///< The method, e.g. "project.file_delete".
    std::string params;                                 ///< Compact JSON params (cut at 512 bytes).
    PermissionClass permission = PermissionClass::Read; ///< The first class that needs approval.
    u8 permissions = 0; ///< Every class that needs approval (bit 1 << PermissionClass), e.g. of a batch.
};

/// An open connection, for the editor's AI Activity panel.
struct ClientInfo
{
    u32 connection = 0;       ///< Connection id.
    std::string client;       ///< Client name.
    std::string origin;       ///< Origin of its commands, `rpc:<client>`.
    std::string agent;        ///< Agent reported with `editor.set_client` ("" if not).
    std::string agentVersion; ///< Its version.
    std::string era;          ///< MCP protocol era of the bridge, e.g. "2025-06-18".
    u64 requests = 0;         ///< Requests it made.
    bool paused = false;      ///< Its requests are held back (RpcServer::setPaused()).
    u8 allowed = 0;           ///< Permission classes allowed for its session (bit 1 << PermissionClass).
};

/// A finished request in the activity ring (the newest 256).
struct ActivityEntry
{
    u64 sequence = 0;   ///< Increases with every entry.
    u32 connection = 0; ///< The connection.
    std::string client; ///< Its client name.
    std::string method; ///< The method.
    std::string params; ///< Compact JSON params (cut at 200 bytes).
    i32 code = 0;       ///< 0 on success, else the error code.
    u64 micros = 0;     ///< Time it took.
    u64 revision = 0;   ///< Scene revision after it.
};

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
    /// A person can approve requests (the windowed editor): in PermissionMode::Ask, destructive, files, build
    /// and system commands wait for decide() instead of being denied.
    bool interactive = false;
    /// Called from I/O threads after a request was queued (the windowed editor wakes its event loop).
    std::function<void()> wake{};
};

/// Serves the editor protocol.
///
/// I/O threads read and frame messages, answer protocol errors, authentication, `rpc.discover`, unknown
/// methods and permission denials themselves, and queue the rest. The editor's main thread runs the queue
/// with pump(), between frames, so commands never race with the World. Requests of one connection run and
/// are answered in order, except async commands (CommandFlags::async): they start when their request comes
/// up, advance once per pump(), send `rpc.progress` notifications and are answered when they finish, while
/// later requests go on. `rpc.cancel` ends a running async command or skips a queued request (error 1004).
/// Commands run with the connection id as CommandOptions::caller and a gate for the permission mode, so
/// transactions belong to their connection and are cancelled when it closes. Besides the commands of the
/// registry the server implements `editor.subscribe`, `editor.unsubscribe`, `editor.poll_events`,
/// `editor.set_client`, `editor.clients`, `editor.activity` (a ring of the last 256 requests),
/// `editor.quit` and `rpc.cancel`. The method catalogue is taken when the server is created and again
/// after every `commands.changed` event.
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

    /// On the calling (main) thread: advances every running async command once (a frame of it), then runs
    /// queued requests until the queue is empty or @p budget has passed. Returns how many requests ran.
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

    /// The permission mode clients run under.
    [[nodiscard]] PermissionMode permissions() const noexcept;
    /// Changes the permission mode for the requests that arrive from now on.
    void setPermissions(PermissionMode mode) noexcept;

    /// Requests waiting for approval, the oldest first. Main thread only; valid until the next pump(),
    /// decide() or disconnect().
    [[nodiscard]] std::span<const PendingApproval> approvals() const noexcept;
    /// Answers the approval @p id. The request (and the connection's later requests) run at the next pump().
    /// Returns false if no such approval waits.
    bool decide(u64 id, ApprovalDecision decision);

    /// Holds back (@p paused true) or releases the requests of connection @p id: held requests stay queued in
    /// order and run once it is released. Running async commands go on. False for unknown connections.
    bool setPaused(u32 id, bool paused);
    /// Closes connection @p id, as if the client had disconnected: its transaction is cancelled and its
    /// queued requests are dropped. False for unknown connections.
    bool disconnect(u32 id);

    /// The open connections. Main thread only.
    [[nodiscard]] std::vector<ClientInfo> clients() const;
    /// Appends the activity entries with a sequence greater than @p after to @p out, oldest first. Main
    /// thread only.
    void activity(u64 after, std::vector<ActivityEntry>& out) const;
    /// Increases whenever clients(), activity() or approvals() would return something new, so a panel can
    /// keep copies and refresh them only then. Main thread only.
    [[nodiscard]] u64 stateVersion() const noexcept;

private:
    struct Connection;
    struct Shared;

    std::string execute(Connection& connection, const Request& request, bool approved);
    std::string run(Connection& connection, const Request& request, bool approved, i32& code);
    bool startTask(const std::shared_ptr<Connection>& connection, const Request& request, bool approved);
    [[nodiscard]] u8 approvalNeeded(const Connection& connection, const Request& request) const;
    void release(const std::shared_ptr<Connection>& connection);
    void takeCancels();
    void advanceTasks();
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
