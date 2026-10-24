#pragma once

/// @file
/// A dual-era Model Context Protocol server over a ToolProvider: the stateful legacy era (`initialize`,
/// protocol versions 2025-06-18 and 2025-11-25) and the stateless modern era (per-request `_meta`, version
/// 2026-07-28), with tools generated from editor commands, resources, prompts, progress and cancellation
/// (ADR 0039). It is pure: messages come in as lines and go out through an McpTransport.

#include <illusion/core/Base.hpp>
#include <illusion/editor/mcp/McpCatalog.hpp>

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::mcp
{

/// The modern protocol version (stateless, per-request `_meta`).
inline constexpr std::string_view kModernVersion = "2026-07-28";
/// Legacy protocol versions (an `initialize` handshake), newest first.
inline constexpr std::string_view kLegacyVersions[] = {"2025-11-25", "2025-06-18"};
/// `_meta` key of the protocol version of a modern request.
inline constexpr std::string_view kProtocolVersionKey = "io.modelcontextprotocol/protocolVersion";
/// JSON-RPC error code of an unsupported protocol version (modern era).
inline constexpr i32 kUnsupportedProtocolVersion = -32022;

/// Which protocol era a client speaks.
enum class McpEra : u8
{
    None,   ///< Not known yet.
    Legacy, ///< `initialize` handshake.
    Modern, ///< Stateless requests with `_meta`.
};

/// Who is connected, as the client reports it.
struct McpClientInfo
{
    std::string name;            ///< `clientInfo.name`, e.g. "claude-code".
    std::string version;         ///< `clientInfo.version`.
    std::string protocolVersion; ///< The negotiated (legacy) or requested (modern) protocol version.
    McpEra era = McpEra::None;   ///< Its era.
};

/// The outcome of a command run by a ToolProvider.
struct CallResult
{
    bool ok = true;                   ///< False if the command failed.
    std::string json;                 ///< The result object, or the error object `{code, message, data}`.
    std::vector<std::string> notes{}; ///< Extra lines for the agent, e.g. that the editor was relaunched.
};

/// What a ToolProvider may do while a command runs.
class CallControl
{
public:
    virtual ~CallControl() = default;

    /// True once the client cancelled the call; the provider should stop and return soon.
    [[nodiscard]] virtual bool cancelled() const noexcept = 0;

    /// Reports progress: @p done of @p total (0: unknown) with an optional @p message.
    virtual void progress(f64 done, f64 total, std::string_view message) = 0;

    /// Handles client messages that arrived meanwhile (cancellations and pings now, other requests after the
    /// call). Call it regularly while waiting.
    virtual void service() = 0;

protected:
    CallControl() = default;
    CallControl(const CallControl&) = default;
    CallControl& operator=(const CallControl&) = default;
};

/// What the server offers as tools: an editor, or a fake in tests.
class ToolProvider
{
public:
    virtual ~ToolProvider() = default;

    /// The commands (empty while no editor is reachable).
    [[nodiscard]] virtual const Catalog& catalog() = 0;

    /// Increases whenever catalog() changes.
    [[nodiscard]] virtual u64 catalogRevision() const noexcept = 0;

    /// Runs @p method with @p params (a JSON object as text).
    virtual CallResult call(std::string_view method, std::string_view params, CallControl& control) = 0;

protected:
    ToolProvider() = default;
    ToolProvider(const ToolProvider&) = default;
    ToolProvider& operator=(const ToolProvider&) = default;
};

/// Where the server's messages go and where messages that arrive during a call come from.
class McpTransport
{
public:
    virtual ~McpTransport() = default;

    /// Sends one message (without the line break).
    virtual void send(std::string_view line) = 0;

    /// The next message that has arrived, without waiting (nothing if none has).
    [[nodiscard]] virtual std::optional<std::string> poll() = 0;

protected:
    McpTransport() = default;
    McpTransport(const McpTransport&) = default;
    McpTransport& operator=(const McpTransport&) = default;
};

/// Settings of an McpServer.
struct McpServerOptions
{
    std::string name = "illusion_mcp"; ///< `serverInfo.name`.
    std::string version = "0.0.0";     ///< `serverInfo.version`.
    ToolSelection tools{};             ///< Which commands become tools.
    usize pageSize = 100;              ///< Tools, resources and prompts per list page.
    usize maxText = 4096;              ///< Bytes of the text summary of a result.
    u32 maxImageSize = 1024;           ///< Longest side of an image result, in pixels.
    u32 listTtlMs = 300000;            ///< `ttlMs` of modern list results.
    /// Called when a client identifies itself (legacy `initialize`, or a modern request with new client
    /// info), e.g. to tell the editor who is connected.
    std::function<void(const McpClientInfo&)> clientChanged{};
};

/// Instructions sent to clients: the workflow, units and entity references.
[[nodiscard]] std::string_view serverInstructions() noexcept;

/// Serves MCP on one stdio connection. Not thread-safe: call it from one thread.
///
/// handle() takes one message at a time. A tool call runs to completion inside it; meanwhile the provider
/// calls CallControl::service(), which handles cancellations and pings at once and keeps other requests for
/// after the call, in order.
class McpServer
{
public:
    /// A server offering the tools of @p provider; replies go to @p transport. Both must outlive it.
    McpServer(ToolProvider& provider, McpTransport& transport, McpServerOptions options = {});
    McpServer(const McpServer&) = delete;            ///< Not copyable.
    McpServer& operator=(const McpServer&) = delete; ///< Not copyable.
    /// Destroys the server; call close() first to end subscriptions gracefully.
    ~McpServer();

    /// Handles one message (a line without the line break), then the requests that arrived meanwhile.
    void handle(std::string_view line);

    /// Rebuilds the tools if the provider's catalogue changed and notifies clients that asked for it
    /// (`notifications/tools/list_changed`). handle() does this too.
    void refresh();

    /// Ends modern subscriptions with a final response (graceful closure).
    void close();

    /// The connected client (era None until one identifies itself).
    [[nodiscard]] const McpClientInfo& client() const noexcept { return m_client; }

    /// The current tools.
    [[nodiscard]] const std::vector<ToolDefinition>& tools() const noexcept { return m_tools; }

private:
    class Control;
    struct Message;
    struct Subscription
    {
        std::string id; // JSON-RPC id as compact JSON
        bool tools = false;
    };

    void process(std::string_view line);
    void serviceLine(std::string_view line);
    void dispatch(Message& message);
    void handleModern(Message& message, std::string_view version);
    void handleLegacy(Message& message);
    void initialize(Message& message);
    void listTools(Message& message);
    void callTool(Message& message);
    void listResources(Message& message);
    void listResourceTemplates(Message& message);
    void readResource(Message& message);
    void listPrompts(Message& message);
    void getPrompt(Message& message);
    void listen(Message& message);
    void cancel(const Message& message);
    void reply(const Message& message, std::string result, bool cacheable = false, u32 ttlMs = 0);
    void fail(const Message& message, i32 code, std::string_view text, std::string_view data = {});
    void updateClient(const Message& message);
    void rebuild();
    void runDeferred();

    ToolProvider& m_provider;
    McpTransport& m_transport;
    McpServerOptions m_options;
    McpClientInfo m_client;
    std::vector<ToolDefinition> m_tools;
    u64 m_catalogRevision = 0;
    bool m_built = false;
    bool m_legacyReady = false; // legacy: initialize answered
    std::vector<Subscription> m_subscriptions;
    std::deque<std::string> m_deferred;      // requests that arrived during a call
    std::vector<std::string> m_cancelledIds; // ids of deferred requests the client cancelled
    Control* m_active = nullptr;             // the running tool call
};

} // namespace illusion::editor::mcp
