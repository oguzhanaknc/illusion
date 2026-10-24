# AI Agents

An AI agent such as Claude Code controls the editor through `illusion_mcp`, the editor's Model Context
Protocol (MCP) bridge. Every editor command is an MCP tool, so an agent can do everything the editor's own
panels can: create projects and entities, set Components, move things around, undo, save and inspect the
result. Design: [ADR 0039](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0039-mcp-bridge.md).

```text
Claude Code  --MCP (stdio)-->  illusion_mcp  --editor protocol-->  illusion_editor
```

The bridge is a separate process. It attaches to an editor that is already running for the project, or
launches a headless one, and starts it again if it crashes.

## Setting up Claude Code

A project made with `project.create` already contains a `.mcp.json` that names the `illusion_mcp` next to
the editor. Start Claude Code in the project directory and approve the `illusion` server when asked.

For an existing project, add the server yourself:

```bash
claude mcp add --transport stdio illusion -- /path/to/build/bin/Release/illusion_mcp --project .
```

or write `.mcp.json` in the project root:

```json
{"mcpServers": {"illusion": {"type": "stdio", "command": "/path/to/illusion_mcp", "args": ["--project", "."], "timeout": 600000}}}
```

The tutorial [Your first agent session](../../tutorials/first-agent-session.md) walks through a session.

## Bridge options

| Option | Meaning |
| --- | --- |
| `--project <dir>` | The project: its endpoint file (attach) or the project to open (launch). |
| `--auto` | Default: attach if the project's editor is running with `--rpc-listen`, otherwise launch one. |
| `--attach` | Only attach; fail if no editor is running. |
| `--launch` | Always launch a headless editor (its log goes to the bridge's stderr). |
| `--headless`, `--no-gpu`, `--deterministic`, `--seed <n>` | Passed to a launched editor. |
| `--permissions read-only\|ask\|auto` | Permission mode of a launched editor. |
| `--editor <path>`, `--editor-arg <arg>` | Another editor executable, extra editor arguments. |
| `--tools all\|core\|<domain>,...` | Which commands become tools (see below). |
| `--record <file.jsonl>` | Record every MCP message with a timestamp and direction. |
| `--page-size <n>` | Tools, resources and prompts per list page (default 100). |
| `--log-level trace\|debug\|info\|warn\|error\|off` | Log level of the bridge (stderr). |

stdout carries only MCP messages; every log line goes to stderr. The bridge ends when its stdin closes and
asks a launched editor to quit.

## Tools

Each command becomes the tool named after it with `.` replaced by `_`: `entity.create` is
`entity_create`, `project.file_read` is `project_file_read`. A tool's description is the command's summary
with an example, then its parameters (types, ranges, enum values, required or optional), its permission
class and whether it runs long. Its input and output schemas are the command's JSON Schemas, and the hints
`readOnlyHint`, `destructiveHint` and `idempotentHint` come from the command's flags.

`--tools core` offers only the `editor`, `project`, `scene`, `entity`, `component`, `hierarchy`,
`transform`, `history` and `log` commands, which keeps the agent's context small; `--tools entity,scene`
picks domains. Two escape hatches remain:

- `illusion_call {"method": "asset.list", "params": {}}` runs any command, offered as a tool or not.
- `illusion_batch` runs `editor.batch` when the `editor` domain is not selected.

When the editor's commands change (a module registers new ones), clients get
`notifications/tools/list_changed`.

## Results and errors

A result has the command's full result as `structuredContent` and a text summary of at most about 4 KB.
When the summary is cut, it says so; ask for less with `limit` and `cursor` parameters where a tool has them.
Images (screenshots, previews) come as PNG image content of at most 1024 pixels on the long side.

A failed command returns `isError: true` with:

```json
{"code": 1200, "kind": "not_found", "message": "no entity is named 'Board'", "path": "/entity", "hint": "Look up existing names with scene_tree, entity_find, component_types or asset_list.", "errors": [{"path": "/entity", "message": "no entity is named 'Board'"}], "suggestions": [], "retryable": false}
```

`code` and `kind` are those of the [editor protocol](../../reference/editor-protocol.md#errors); `path`
points at the bad parameter. Unknown tools and malformed messages get JSON-RPC errors instead.

## Resources and prompts

| Resource | Contents |
| --- | --- |
| `illusion://guide` | Workflow, units, entity references, undo, permissions, limits. |
| `illusion://commands` | Every command with its tool name and summary. |
| `illusion://project` | The open project (`project.info`). |
| `illusion://scene/current` | The entity tree of the active scene (`scene.tree`). |
| `illusion://scene/summary` | Path, revision, entity count, content hash, unsaved state (`scene.info`). |
| `illusion://log/recent` | The last 50 editor log messages. |
| `illusion://components/{stableName}` | The schema of a Component type, e.g. `illusion://components/illusion.Transform`. |

The prompts `new_game` (`name`, `idea`) and `add_component_type` (`name`, `fields`) start an agent on those
tasks with the right tools.

## Permission modes

The editor's permission mode decides what the agent may run:

| Mode | The agent may |
| --- | --- |
| `read-only` | only read (scene, entities, files). |
| `ask` | read and edit; file, build, destructive and system commands need a person's approval (denied while the editor has no window). |
| `auto` | run everything. The default of a headless editor. |

A denied call fails with kind `permission_denied`. Every change an agent makes has the origin
`rpc:illusion_mcp`, so `history.undo {"scope": "mine"}` undoes only the agent's steps and the editor's
client list (`editor.clients`) and activity ring (`editor.activity`) show who did what.

## Limits

| Limit | Value |
| --- | --- |
| Text summary of a result | about 4 KB (the full result is in `structuredContent`) |
| Image size | 1024 pixels on the long side |
| Tools per list page | 100 (`--page-size`) |
| Tool timeout in `.mcp.json` | 600 s |
| Message size | 16 MiB |

## Crashes and recovery

If a launched editor ends unexpectedly, the call that was running returns kind `editor_crashed` with the
exit code and the editor's last 50 log lines. The next call starts the editor again with the same project
(or the one the agent created or opened last), replays its crash journal with `project.recover`, and adds a
note about it to the result. An attached editor that goes away gives `editor_unavailable`; the next call
connects again.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| The server does not start | Run the command from `.mcp.json` in a terminal: errors are on stderr. Exit code 1 means the editor could not be reached or started; check `--project` and `--editor`. |
| `MCP_PROTOCOL_NEGOTIATION` or `Unsupported protocol version` (-32022) | The client asked for a protocol version the bridge does not speak. It speaks 2025-06-18 and 2025-11-25 (with `initialize`) and 2026-07-28 (stateless); update the client or the bridge. |
| `send initialize first` (-32600) | A client sent requests without `initialize` and without the modern `_meta` version. |
| Tools are missing | Check `--tools`; `illusion_call` reaches every command. |
| Calls fail with `permission_denied` | The editor runs in `read-only` or `ask` mode; launch it with `--permissions auto`. |
| Text summaries end with `truncated` | The result is complete in `structuredContent`; narrow the request with `limit`. |
| `editor_crashed` | The editor's last log lines are in the error; call again to relaunch and recover. |

Record a session with `--record session.jsonl` to see every message.
