# 0039. A dual-era MCP bridge with tools generated from editor commands

- Status: Accepted
- Date: 2026-10-24

## Context

AI agents such as Claude Code reach tools through the Model Context Protocol (MCP), not through our editor
protocol (ADR 0037). The brief wants the editor fully controllable by agents, so every editor command must be
an MCP tool, with nothing hand-written per command that could fall behind the catalogue.

MCP itself is changing under us. Revisions up to 2025-11-25 open a stateful session with `initialize`
(Claude Code speaks 2025-06-18 today). Revision 2026-07-28 removed the handshake: every request carries its
protocol version and client info in `_meta`, servers must implement `server/discover`, every result carries
`resultType`, list results carry `ttlMs`/`cacheScope`, `ping` is gone, list changes arrive only through
`subscriptions/listen`, and an unknown version is rejected with -32022 (`UnsupportedProtocolVersionError`,
with `data.supported`). Clients will speak either era for a long time.

Where the MCP endpoint lives matters too. Inside the editor it could not reach an editor the user already has
open, would die with an editor crash, and would bind the editor to a fast-moving spec.

## Decision

- **A separate bridge process, `illusion_mcp`.** It speaks MCP as NDJSON on stdio and the editor protocol to
  the editor. Modes: `--attach` (the project's endpoint file), `--launch` (a headless editor over stdio, log
  captured) and `--auto`, the default (attach if a live endpoint exists, otherwise launch). Logs go to stderr
  only; it exits when stdin ends, quitting a launched editor.
- **A pure protocol library, `illusion::mcp_protocol`.** It depends on core Json only: an `McpServer` that
  takes lines and writes through an `McpTransport`, over a `ToolProvider` (the editor, or a fake in tests).
  Both eras are served by one process: an `initialize` selects legacy semantics (the requested version if it
  is 2025-06-18 or 2025-11-25, otherwise 2025-11-25 and the client decides); a request with
  `_meta["io.modelcontextprotocol/protocolVersion"]` is served statelessly when it names 2026-07-28 and gets
  -32022 otherwise. Requests without `_meta` before `initialize` get -32600.
- **Tools are generated one to one from the catalogue.** The bridge reads `rpc.discover` (OpenRPC, ADR 0037)
  and skips server methods. The tool name is the method with every character outside `[a-z0-9_]` replaced by
  `_` (`entity.create` → `entity_create`), in catalogue order; the description is the command's English
  agent summary plus generated parameter documentation (types, ranges, enum values, required or optional),
  the common mutating options once, the permission class and whether it is long-running; `inputSchema` and
  `outputSchema` are the command's JSON Schemas; annotations carry `readOnlyHint`, `destructiveHint`,
  `idempotentHint` and `openWorldHint: false`. `--tools all|core|<domains>` filters by domain;
  `illusion_call {method, params}` always reaches every command, and `illusion_batch` stands in for
  `editor.batch` when the `editor` domain is filtered out. `commands.changed` reloads the catalogue and
  sends `notifications/tools/list_changed` (legacy) or the same notification on `subscriptions/listen`
  streams that asked for `toolsListChanged` (modern). Lists page at 100 entries with numeric cursors.
- **Results.** `structuredContent` is the command result, whole. The text block is a compact JSON summary cut
  at 4 KB on a UTF-8 boundary, with a hint to narrow the request. A result member `_images` (array of
  `{mimeType, data}`) moves into image content blocks; PNGs longer than 1024 pixels on a side are replaced by
  a text note (commands that return images will default to 768). A failed command returns `isError:
  true` with the flat object `{code, kind, message, path, hint, errors, suggestions, retryable, ...}` (`path`
  is the first error's JSON Pointer, `hint` is generated from the kind and suggestions). Malformed MCP
  traffic and unknown tools get JSON-RPC errors.
- **Progress and cancellation.** `_meta.progressToken` turns the editor's `rpc.progress` into
  `notifications/progress`. While a call runs, the provider calls `CallControl::service()`: cancellations
  and legacy pings are handled at once, other requests wait and run afterwards in order. A cancelled running
  call sends `rpc.cancel` to the editor and gets no response; a cancelled waiting request never runs.
- **Resources and prompts.** `illusion://guide` and `illusion://commands` are written by the bridge;
  `illusion://project`, `scene/current`, `scene/summary`, `log/recent` and the template
  `illusion://components/{stableName}` read through commands (listed only when the editor has them). Prompts
  `new_game` and `add_component_type` give agents the workflow. Instructions name the workflow (discover,
  create, save, verify), units and entity references.
- **Crashes.** When a launched editor dies, the failing call returns kind `editor_crashed` with the exit code
  and the editor's last 50 log lines; the next call relaunches it (with the project the agent last created or
  opened) and runs `project.recover`, noting what it replayed in the result. An attached editor that goes away
  gives `editor_unavailable`, and the next call reconnects. Deterministic sessions reuse session ids, so the
  editor never truncates a crashed session's journal of the same name.
- **The editor knows its agents.** `editor.set_client {client, agent, agentVersion, era}` names a
  connection (its origin becomes `rpc:<client>`, so bridge changes are `rpc:illusion_mcp`), `editor.clients`
  lists connections, and `editor.activity` returns a ring of the last 256 requests for the AI activity panel
  (M7l). The editor protocol moves to 0.3.0.
- **`project.create` writes `.mcp.json`** naming the `illusion_mcp` next to the editor with `--project .`,
  so Claude Code started in a new project reaches its editor.
- **Tests.** Golden transcripts per era (`editor/tests/data/mcp/golden/<version>/`) run against a fake
  provider byte for byte and against a real headless editor by outcome; the tetris board acceptance scenario
  runs through the bridge in both eras (`illusion_ctl replay --via-mcp --era legacy|modern`); stdout is
  checked to carry only protocol lines at every log level.

## Consequences

- A new command needs nothing for MCP: its agent summary and schemas are its tool. The quality of
  `agentSummary` (with an example) and of field metadata now directly shapes what agents can do.
- Two processes per agent session (bridge and editor) and one extra hop per call; `mcp/translate` costs about
  1.6 µs per call.
- Spec churn stays in `mcp_protocol` and its goldens. Revisions differ inside an era only in the version
  string we echo; when they diverge, the era handlers grow per-version branches.
- Not yet: the `illusion://sdk/{header}` and `illusion://manual/{page}` resources (they need the read-only
  `sdk:` mount and shipped manual pages, M7s/M8), MCP Streamable HTTP for remote agents (not planned in M7),
  normalised recordings of live sessions (`--record` writes raw lines; normalisation comes with the nightly
  live-agent job), and approval prompts for the `ask` mode (M7l).
