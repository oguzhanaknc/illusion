#pragma once

/// @file
/// The commands an MCP bridge offers as tools: read from the editor's OpenRPC document, named and described
/// one to one, and filtered by domain (ADR 0039).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::mcp
{

/// One editor command as the bridge sees it.
struct CommandSpec
{
    std::string name;           ///< Editor method, e.g. "entity.create".
    std::string description;    ///< English agent summary with an example.
    std::string category{};     ///< Category, e.g. "Entity".
    std::string permission{};   ///< Permission class, e.g. "edit".
    u32 version = 1;            ///< Command version.
    bool readOnly = false;      ///< Never changes editor state.
    bool destructive = false;   ///< May discard work.
    bool idempotent = false;    ///< Repeating it has no further effect.
    bool async = false;         ///< Runs over several frames with progress and cancellation.
    std::string inputSchema{};  ///< JSON Schema 2020-12 of the params (a compact JSON object).
    std::string outputSchema{}; ///< JSON Schema 2020-12 of the result (a compact JSON object).
};

/// The commands of an editor, in catalogue order.
struct Catalog
{
    std::vector<CommandSpec> commands; ///< Every command.

    /// The command named @p method, or nullptr.
    [[nodiscard]] const CommandSpec* find(std::string_view method) const noexcept;
};

/// Reads the commands from an OpenRPC 1.3 document as `rpc.discover` returns it (ADR 0037). Methods the
/// server implements itself (`x-illusion.server`) are skipped: they act on the connection, not the editor.
[[nodiscard]] Expected<Catalog, std::string> catalogFromOpenRpc(std::string_view document);

/// The tool name of @p method: every character that is not `[a-z0-9_]` becomes `_` ("entity.create" ->
/// "entity_create").
[[nodiscard]] std::string toolName(std::string_view method);

/// True if @p name is a valid tool name: 1 to 64 characters of `[a-z0-9_]`.
[[nodiscard]] bool isValidToolName(std::string_view name) noexcept;

/// Domains of `--tools core`: what an agent needs to build a scene.
inline constexpr std::string_view kCoreDomains[] = {
    "editor", "project", "scene", "entity", "component", "hierarchy", "transform", "history", "log"};

/// Which commands become tools (`--tools all|core|<domain>,<domain>...`).
class ToolSelection
{
public:
    /// Every command.
    ToolSelection() = default;

    /// Parses `all`, `core` or a comma-separated list of domains (the part of a method before the dot,
    /// e.g. `entity,component`; `core` may be one of them).
    [[nodiscard]] static Expected<ToolSelection, std::string> parse(std::string_view text);

    /// True if @p method becomes a tool.
    [[nodiscard]] bool includes(std::string_view method) const noexcept;

    /// True if every command becomes a tool.
    [[nodiscard]] bool all() const noexcept { return m_domains.empty(); }

private:
    std::vector<std::string> m_domains;
};

/// Name of the escape hatch that runs any command of the catalogue, selected or not.
inline constexpr std::string_view kCallTool = "illusion_call";
/// Name of the escape hatch for `editor.batch` when the `editor` domain is not selected.
inline constexpr std::string_view kBatchTool = "illusion_batch";

/// A tool generated from a command (or an escape hatch).
struct ToolDefinition
{
    std::string name;   ///< Tool name.
    std::string method; ///< Editor method it runs ("" for illusion_call, which takes the method).
    std::string json;   ///< The MCP Tool object as compact JSON.
};

/// The tools of @p catalog under @p selection, in catalogue order, then the escape hatches.
[[nodiscard]] std::vector<ToolDefinition> generateTools(const Catalog& catalog,
                                                        const ToolSelection& selection);

/// The description of the tool of @p command: its summary, then its parameters (types, ranges, enum names,
/// whether required) and its permission class and flags.
[[nodiscard]] std::string toolDescription(const CommandSpec& command);

/// The tool definition named @p name in @p tools, or nullptr.
[[nodiscard]] const ToolDefinition* findTool(std::span<const ToolDefinition> tools,
                                             std::string_view name) noexcept;

} // namespace illusion::editor::mcp
