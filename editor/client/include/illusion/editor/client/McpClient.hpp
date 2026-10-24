#pragma once

/// @file
/// A minimal MCP client of `illusion_mcp`: launches the bridge, speaks the legacy or the modern era and runs
/// editor methods as tool calls. The acceptance runner uses it to replay transcripts through the real MCP
/// layer (`illusion_ctl replay --via-mcp`, ADR 0039).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/editor/client/RpcClient.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::client
{

/// How to start the bridge for McpClient::launch().
struct McpLaunchOptions
{
    std::filesystem::path bridge;             ///< The `illusion_mcp` executable.
    std::vector<std::string> arguments{};     ///< Its arguments.
    std::filesystem::path workingDirectory{}; ///< Directory to start it in (empty: the current one).
    bool modern = false;                      ///< Speak the modern era (2026-07-28) instead of legacy.
    std::string legacyVersion = "2025-06-18"; ///< Protocol version the legacy handshake asks for.
    std::string clientName = "illusion_ctl";  ///< `clientInfo.name`.
};

/// A connection to `illusion_mcp`. Not thread-safe.
class McpClient final : public CallChannel
{
public:
    /// Not connected.
    McpClient() noexcept = default;
    /// Takes over @p other.
    McpClient(McpClient&& other) noexcept = default;
    /// Closes the current connection and takes over @p other.
    McpClient& operator=(McpClient&& other) noexcept = default;
    McpClient(const McpClient&) = delete;            ///< Not copyable.
    McpClient& operator=(const McpClient&) = delete; ///< Not copyable.
    /// Closes the connection (see close()).
    ~McpClient() override;

    /// Starts the bridge and performs the handshake of the era (`initialize` and `notifications/initialized`,
    /// or `server/discover`).
    [[nodiscard]] static Expected<McpClient, std::string> launch(const McpLaunchOptions& options);

    /// Sends the MCP request @p method with @p params (a JSON object as text); in the modern era the
    /// protocol version and client info are added to `_meta`. Returns the result as compact JSON.
    [[nodiscard]] Expected<std::string, RpcError>
    request(std::string_view method, std::string_view params,
            std::chrono::milliseconds timeout = kDefaultCallTimeout);

    /// Runs the editor method @p method as the tool named after it. A result with `isError` becomes an
    /// RpcError with the command's code, message and `data` (`{kind, errors, suggestions, retryable}`), as
    /// the editor protocol would report it; otherwise the result is `structuredContent`.
    [[nodiscard]] Expected<std::string, RpcError> call(std::string_view method, std::string_view params,
                                                       std::chrono::milliseconds timeout) override;

    /// True while the bridge runs.
    [[nodiscard]] bool connected() const noexcept override { return m_rpc.connected(); }

    /// The protocol version in use (the server's answer to `initialize`, or the modern version).
    [[nodiscard]] const std::string& protocolVersion() const noexcept { return m_version; }

    /// The result of the handshake (`initialize` or `server/discover`) as compact JSON.
    [[nodiscard]] const std::string& handshake() const noexcept { return m_handshake; }

    /// Closes the bridge's stdin, so it ends (and quits a launched editor), and waits up to @p timeout.
    std::optional<i32> close(std::chrono::milliseconds timeout) override;

private:
    RpcClient m_rpc;
    bool m_modern = false;
    std::string m_version;
    std::string m_handshake;
    std::string m_clientName;
};

} // namespace illusion::editor::client
