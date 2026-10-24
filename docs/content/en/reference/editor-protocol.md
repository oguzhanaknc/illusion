# Editor Protocol

Protocol version `0.2.0` (semver; `1.0` comes with M7x). Design: ADR 0037. How to use it:
[Automation](../manual/editor/automation.md).

## Framing

One UTF-8 JSON object per line, ended by `\n` (a `\r` before it is ignored, empty lines are skipped), at most
16 MiB without the line break. A longer line gets -32600 and the connection closes. JSON-RPC batch arrays are
rejected with -32600.

## Requests and replies

```json
{"jsonrpc": "2.0", "id": 7, "method": "entity.create", "params": {"name": "Board", "clientToken": "c1-0007"}}
{"jsonrpc": "2.0", "id": 7, "result": {"entity": {"id": "9f2c41aa0b7d3e15", "name": "Board", "path": "/Board"}, "_editor": {"revision": 42, "transaction": 17, "dryRun": false, "operations": 2, "warnings": []}}}
```

- `id` is a string, a number or null and comes back unchanged. A message without `id` is a notification: it
  runs, and never gets a reply, not even an error.
- `params` is an object (named parameters) or absent.
- Replies of one connection come in the order of its requests, except replies to async commands: those
  come when the command finishes, after `rpc.progress` notifications, and later requests do not wait for them.
- Every command result has an `_editor` member: the scene revision after the call, the undo transaction (0
  for none), whether it was a dry run, the number of primitive operations and warnings.
- Mutating commands also take `dryRun`, `mergeKey`, `expectRevision` and `clientToken` (see the
  [editor core](../manual/editor/index.md#commands)).

## Errors

```json
{"jsonrpc": "2.0", "id": 7, "error": {"code": 1200, "message": "entity '/Nope' does not exist", "data": {"kind": "not_found", "errors": [], "suggestions": [], "retryable": false}}}
```

| Code | Meaning |
| --- | --- |
| -32700 | The line is not JSON or not valid UTF-8 (`id` is null). |
| -32600 | Not a JSON-RPC 2.0 request object, a batch array, or a line over 16 MiB. |
| -32601 | No such method; `data.suggestions` lists close names. |
| -32602 | `params` is not an object. |
| -32603 | A bug in the server. |
| 1000 | `unauthenticated`: authenticate first, with the right token. |
| 1001 | `permission_denied`: the permission mode does not allow the command. |
| 1002 | `busy`: the request queue is full, or another connection has a transaction open (`retryable`). |
| 1004 | `cancelled`: `rpc.cancel` ended the request. |
| 1003–1500 | Command errors (`ErrorKind`): `timeout`, `wrong_mode`, `not_found`, `already_exists`, `validation_failed` (with `data.errors[{path, message}]`), `conflict`, `ambiguous`, `precondition_failed`, `io_error`, `build_failed`, `module_error`, `unsupported`, `limit_exceeded`, `internal`. |

## Transports

| Transport | Started with | Authentication |
| --- | --- | --- |
| stdio | `--rpc-stdio` | None: the parent process owns the pipes. |
| `AF_UNIX` socket | `--rpc-listen [<socket>]` | `rpc.authenticate` with the token of the endpoint file. |

Endpoint file `<project>/.illusion/run/editor-endpoint.json`: `{pid, protocolVersion, editorVersion,
transport, address, token}`, owner-only, written atomically and deleted on exit. The socket is
`<project>/.illusion/run/editor.sock`, or `illusion-<hash>.sock` in `XDG_RUNTIME_DIR`, `TMPDIR` or the temp
directory when that path is longer than 103 bytes (or not ASCII on Windows).

## Server methods

| Method | Params | Result |
| --- | --- | --- |
| `rpc.authenticate` | `token` (64 hex digits), `client` (name for origins, `[A-Za-z0-9._-]`, up to 64) | `{protocolVersion, editorVersion, client}` |
| `rpc.discover` | — | OpenRPC 1.3 document of every method |
| `editor.subscribe` | `topics` (array; empty: all), `mode` (`push` or `pull`, default `push`) | `{topics, mode}` |
| `editor.unsubscribe` | — | `{}` |
| `editor.poll_events` | `limit` (default 256) | `{events: [{topic, payload}], overflow}` |
| `editor.quit` | — | `{}`; the editor ends after the reply (permission class `system`) |
| `rpc.cancel` | `id` (of a request of this connection) | `{}` if sent with an id; usually a notification |

A socket client's first message must be `rpc.authenticate`, within 2 s; otherwise it gets 1000 and the
connection closes. Push events are notifications `{"method": "event", "params": {"topic", "payload"}}`.
Async commands send `{"method": "rpc.progress", "params": {"id", "progress", "total", "message"}}` (`message`
only when there is one) at most once per frame, when the progress changed. Commands run with the connection as
their caller: a connection's transaction (`transaction.begin`) is cancelled when it closes, and
`history.undo {"scope": "mine"}` undoes the steps of the connection's origin `rpc:<client>`. The steps of
`editor.batch` are checked against the permission mode one by one.
Every other method is an [editor command](editor-commands.md).

## Limits

| Limit | Value | When exceeded |
| --- | --- | --- |
| Message size | 16 MiB | -32600, connection closed |
| Connections | 8 | 1401, connection closed |
| Waiting requests per connection (running async commands included) | 64 | 1401 (`retryable`) |
| Waiting requests in all | 256 | 1002 (`retryable`) |
| Events kept per pull subscription | 1,024 | `overflow: true` |
| Remembered `clientToken`s per client | 1,024 | the oldest is forgotten |
| Steps of `editor.batch` | 1,000 | 1202 |
| Frames of `editor.wait` | 100,000 | 1202 |
| Unsent replies per connection | 64 MiB | connection closed |

## Permission modes

| Mode | Read | Edit | Destructive, Files, Build, System |
| --- | --- | --- | --- |
| `read-only` | read-only commands | denied | denied |
| `ask` | allowed | allowed | need approval (denied headless) |
| `auto` | allowed | allowed | allowed |
