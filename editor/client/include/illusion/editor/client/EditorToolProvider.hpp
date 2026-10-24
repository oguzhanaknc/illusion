#pragma once

/// @file
/// The editor side of the MCP bridge: an mcp::ToolProvider that runs tools as editor commands over the editor
/// protocol, attaching to a running editor or launching a headless one, and relaunching it with recovery
/// after a crash (ADR 0039).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/editor/client/RpcClient.hpp>
#include <illusion/editor/mcp/McpServer.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::client
{

/// How the bridge reaches the editor.
enum class BridgeMode : u8
{
    Auto,   ///< Attach if the project has a live endpoint file, otherwise launch a headless editor.
    Attach, ///< Connect to the editor of the project's endpoint file.
    Launch, ///< Start a headless editor over stdio.
};

/// Settings of an EditorToolProvider.
struct EditorToolProviderOptions
{
    BridgeMode mode = BridgeMode::Auto; ///< How to reach the editor.
    std::filesystem::path project{};    ///< Project to open (launch) or whose endpoint file to read (attach).
    /// The editor to launch and extra arguments (`--project` is added from #project). Its log is captured.
    LaunchOptions launch{};
    std::string client = "illusion_mcp";         ///< Client name: changes get the origin `rpc:<client>`.
    usize logLines = 50;                         ///< Editor log lines kept for crash reports.
    std::function<void(std::string_view)> log{}; ///< Receives every log line of a launched editor.
    std::chrono::milliseconds pollInterval{10};  ///< How long a running call waits for its reply at a time.
};

/// Runs MCP tools as editor commands. Not thread-safe: use it from the thread that runs the McpServer.
class EditorToolProvider final : public mcp::ToolProvider
{
public:
    /// A provider that is not connected yet (see connect()).
    explicit EditorToolProvider(EditorToolProviderOptions options);
    EditorToolProvider(const EditorToolProvider&) = delete;            ///< Not copyable.
    EditorToolProvider& operator=(const EditorToolProvider&) = delete; ///< Not copyable.
    /// Closes the connection (see close()).
    ~EditorToolProvider() override;

    /// Reaches the editor as the mode says and reads its command catalogue.
    [[nodiscard]] Expected<void, std::string> connect();

    /// The commands of the editor (the last known ones while it is unreachable).
    [[nodiscard]] const mcp::Catalog& catalog() override { return m_catalog; }

    /// Increases whenever catalog() changes.
    [[nodiscard]] u64 catalogRevision() const noexcept override { return m_revision; }

    /// Runs @p method. Progress and cancellation go through @p control. If the editor is gone, the call fails
    /// with kind `editor_crashed` (with its last log lines) or `editor_unavailable`, and the next call starts
    /// or reconnects it first; a relaunched editor replays its crash journal (`project.recover`).
    mcp::CallResult call(std::string_view method, std::string_view params,
                         mcp::CallControl& control) override;

    /// Between messages: forwards log lines, notices an editor that ended and reloads the catalogue after
    /// `commands.changed`.
    void poll();

    /// Tells the editor which agent is connected (`editor.set_client`), now and after every reconnect.
    void setAgent(const mcp::McpClientInfo& agent);

    /// True while connected to an editor.
    [[nodiscard]] bool connected() const noexcept { return m_client.connected(); }

    /// True if the current connection attached to a running editor rather than launching one.
    [[nodiscard]] bool attached() const noexcept { return m_attached; }

    /// The launched editor (nullptr when attached or not connected).
    [[nodiscard]] Process* editorProcess() noexcept { return m_client.process(); }

    /// The last log lines of the launched editor, oldest first.
    [[nodiscard]] const std::deque<std::string>& recentLog() const noexcept { return m_log; }

    /// Closes the connection; a launched editor is asked to quit. Returns its exit code.
    std::optional<i32> close();

private:
    Expected<void, std::string> open(bool launch);
    Expected<void, std::string> loadCatalog();
    bool ensureConnected(std::vector<std::string>& notes, mcp::CallResult& failure);
    mcp::CallResult lost(std::string_view method);
    void drainLog();
    void handleNotifications();
    void sendAgent();

    EditorToolProviderOptions m_options;
    RpcClient m_client;
    mcp::Catalog m_catalog;
    std::string m_catalogText;
    u64 m_revision = 0;
    bool m_attached = false;
    bool m_everConnected = false;
    bool m_catalogDirty = false;
    std::optional<i32> m_lastExit; // exit code of an editor that ended while idle
    bool m_endedIdle = false;
    std::filesystem::path m_projectRoot; // the project the agent created or opened last
    std::deque<std::string> m_log;
    mcp::McpClientInfo m_agent;
};

} // namespace illusion::editor::client
