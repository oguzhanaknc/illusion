# Automation

Every editor command can be called from another process: AI agents, scripts, CI jobs and the MCP bridge
use the same commands as the editor's own panels. The editor speaks JSON-RPC 2.0, one JSON object per
line, over its stdin and stdout or over a local socket. Nothing listens on the network.

## Running the editor headless

Without a window (`--headless`), `illusion_editor` only serves the editor protocol:

```bash
illusion_editor --headless --workspace ~/games --rpc-stdio
```

| Option | Meaning |
| --- | --- |
| `--headless` | No window. Without it the editor window opens (see [Editor Window](interface.md)). |
| `--project <dir>` | Open this project at start. |
| `--workspace <dir>` | The directory where `project.create` and `project.open` may work (default: the parent of `--project`, or the current directory). |
| `--rpc-stdio` | Serve the parent process over stdin and stdout. The editor ends when stdin closes. |
| `--rpc-listen [<socket>]` | Serve local clients over an `AF_UNIX` socket (default: in the project, see below). |
| `--permissions read-only\|ask\|auto` | What clients may run (default `auto`). |
| `--deterministic`, `--seed <n>` | Seeded entity ids and a fixed 1/60 s frame step, for reproducible sessions. |
| `--no-gpu` | Run without a GPU; commands that need one (`editor.screenshot`, the `ui.*` commands) fail with `unsupported`. With a GPU the UI runs offscreen. |
| `--ui-size <w>x<h>` | Size of the offscreen UI for `editor.screenshot` (default 1920x1080). |
| `--user-dir <dir>` | Preferences and saved layouts (default: kept in memory). |
| `--frames <n>` | Stop after `n` frames. |

The log goes to stderr, so stdout carries only the protocol.

## illusion_ctl

`illusion_ctl` is the command-line client. It either launches a headless editor for one command, or
connects to a running one:

```bash
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --workspace --editor-arg ~/games \
    call project.create '{"directory": "tetris", "name": "Tetris"}'
illusion_ctl --project ~/games/tetris call scene.info
illusion_ctl --project ~/games/tetris subscribe scene.changed history.changed --count 10
illusion_ctl --project ~/games/tetris dump-catalog > catalog.json
```

| Command | What it does |
| --- | --- |
| `call <method> [<params>]` | Runs one command and prints its result (exit code 1 and the error on stderr if it fails). |
| `batch <file.jsonl> [--keep-going]` | Runs one `{"method", "params"}` object per line, in order. |
| `subscribe [<topic>...] [--count <n>]` | Prints editor events as they arrive. |
| `dump-catalog` | Prints the OpenRPC document of every method (`rpc.discover`). |
| `replay <transcript> [--checks <file>]` | Replays a transcript against a launched editor (see [acceptance tests](#acceptance-tests)). |
| `check <file>` | Runs a checks file against a launched editor. |

`replay` and `check` take `--via-mcp <illusion_mcp> [--era legacy|modern]` to run every step as an MCP tool
call through the bridge instead (see [AI agents](ai-agents.md)).

Connections: `--launch <editor>` (with `--editor-arg` for each editor argument), `--project <dir>` (the
project's endpoint file), `--endpoint <file>`, or `--socket <path> --token <token>`.

## The Python client

`tools/editor/illusion_rpc.py` needs only the Python standard library:

```python
from illusion_rpc import EditorClient, RpcError

with EditorClient.launch("build/bin/Release/illusion_editor", ["--workspace", "/tmp/games"]) as editor:
    editor.call("project.create", directory="tetris", name="Tetris")
    board = editor.call("entity.create", name="Board")["entity"]
    try:
        editor.call("entity.get", entity="/Nope")
    except RpcError as error:
        print(error.code, error.data["kind"])  # 1200 not_found
```

Notifications such as `event` and `rpc.progress` collect in `editor.notifications` while `call()` waits.
`EditorClient.connect_project(path)` connects to a running editor through its endpoint file.

## Sockets and the endpoint file

With `--rpc-listen` and a project, the editor listens at `<project>/.illusion/run/editor.sock` and writes
`<project>/.illusion/run/editor-endpoint.json`:

```json
{"pid": 4242, "protocolVersion": "0.1.0", "editorVersion": "0.1.0", "transport": "unix", "address": "/home/me/games/tetris/.illusion/run/editor.sock", "token": "<64 hex digits>"}
```

Without `--project`, pass a socket path; the endpoint file is then written next to it as
`<socket>.endpoint.json` (use `illusion_ctl --endpoint`). The run directory and the file are readable by the
current user only. A socket client must send
`rpc.authenticate {"token": ..., "client": "my-tool"}` as its first message within 2 seconds; the client name
appears in the undo history as the origin `rpc:my-tool`. When the project path is too long for a socket
address (or, on Windows, not ASCII), the socket moves to `illusion-<hash>.sock` in the per-user runtime or
temp directory; the endpoint file always says where it is. The file is deleted when the editor exits;
clients ignore a file whose editor process is gone.

## Permissions and safety

| Mode | Runs |
| --- | --- |
| `read-only` | Only read-only commands. |
| `ask` | Read and edit commands; destructive, file, build and system commands need a person's approval, so a headless editor denies them. |
| `auto` | Everything (the headless default). |

Denied calls fail with `permission_denied` (1001). File commands stay inside the project mounts, and project
commands inside the workspace. There is no command that runs a shell.

!!! warning "Agents can run code"
    From M7s, `module.build` and `module.load` compile and load C++ that an agent wrote, inside the editor
    process. Use `read-only` or `ask` for agents you do not trust.

## Events

`editor.subscribe {"topics": ["scene.changed"], "mode": "push"}` makes the editor send a notification for
each event, once per frame and topic:

```json
{"jsonrpc": "2.0", "method": "event", "params": {"topic": "scene.changed", "payload": {"revision": 42}}}
```

In `pull` mode the editor keeps up to 1,024 events for `editor.poll_events`; when more arrive, the reply says
`"overflow": true` and the client reads the scene again. Topics include `scene.changed`, `scene.opened`,
`scene.saved`, `selection.changed`, `history.changed`, `scene.autosaved` and `commands.changed`; an empty list
means all. After `commands.changed` (commands were registered, e.g. by a module) the server's method list and
`rpc.discover` include the new commands.

Events are published at the end of a frame, so a command reply does not guarantee that its events are
already available. For a deterministic pull, await `editor.wait {"frames": 1}` after the command before
calling `editor.poll_events`. Additional synchronous calls do not guarantee a frame boundary.

## Async commands and cancellation

Commands flagged `async` (`editor.list_commands` shows the flag) take several editor frames. The editor
answers them when they finish and sends `rpc.progress` notifications with the request id in between; the
connection's later requests do not wait for them. `rpc.cancel` with the id ends a running async command, or
skips a request that is still queued, and the request is answered with `cancelled` (1004):

```json
{"jsonrpc": "2.0", "id": 7, "method": "editor.wait", "params": {"frames": 600}}
{"jsonrpc": "2.0", "method": "rpc.progress", "params": {"id": 7, "progress": 1, "total": 600}}
{"jsonrpc": "2.0", "method": "rpc.cancel", "params": {"id": 7}}
{"jsonrpc": "2.0", "id": 7, "error": {"code": 1004, "message": "editor.wait was cancelled", "data": {"kind": "cancelled", "errors": [], "suggestions": [], "retryable": false}}}
```

## Batches and transactions

`editor.batch` runs several commands as one undo step, with `$` references from one step's result to the
next ([editor core](index.md#batches)). For a longer piece of work a client can open a transaction of its
connection; meanwhile other clients cannot change the scene (`busy`, retryable), and if the connection
closes the editor reverts the transaction:

```python
editor.call("transaction.begin", label="Build board")
board = editor.call("entity.create", name="Board")["entity"]
editor.call("entity.create", name="Cell 1", parent=board["id"])
editor.call("transaction.commit")  # one undo step; transaction.cancel would revert both
editor.call("history.undo", scope="mine")  # undoes it even if other clients edited since
```

## Retries and concurrency

- `clientToken` on a mutating call makes it safe to retry: the editor remembers the last 1,024 tokens of each
  client and answers a repeated one with the first reply.
- `expectRevision` fails with `conflict` (1203) when someone else changed the scene since you read it.
- `history.undo {"scope": "mine"}` undoes your own last step past later independent steps of other clients,
  and fails with `conflict` when they touched the same things.
- `busy` (1002) and `limit_exceeded` (1401) with `retryable: true` mean "try again later": the editor accepts
  8 connections, 64 waiting requests per connection and 256 in all.

## Acceptance tests

The agent acceptance test replays a transcript of editor calls and checks the result
(`tests/acceptance/tetris`). Each line of a transcript is one step:

```json
{"call": "entity.create", "params": {"name": "Board"}, "capture": {"board": "/entity/id"}, "expect": {"result": {"entity": {"path": "/Board"}}}}
{"call": "entity.get", "params": {"entity": "${board}"}}
{"restart": {"arguments": ["--project", "${root}"]}}
```

`capture` saves values from a reply by JSON Pointer and `${name}` uses them later. `expect` matches the reply:
objects match when they have at least the given members, and `$approx`, `$regex`, `$length`, `$exists`,
`$oneOf` and `$contains` relax the match. A step marked `"requires": "gpu"` must match `otherwise` (error 1400
by default) on an editor without a GPU. Run it with:

```bash
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --deterministic --editor-arg --no-gpu \
    replay tests/acceptance/tetris/scripted.jsonl --checks tests/acceptance/tetris/checks.jsonl --temp-workspace
```

CTest also runs the scenario through the MCP bridge in both protocol eras
(`acceptance.tetris.board.mcp-legacy` and `acceptance.tetris.board.mcp-modern`, with `--via-mcp`).

The protocol itself is described in the [editor protocol reference](../../reference/editor-protocol.md).
