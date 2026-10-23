# 0037. Editor RPC protocol: JSON-RPC 2.0 over NDJSON on stdio and AF_UNIX, tokens and permission modes

- Status: Accepted
- Date: 2026-10-23

## Context

The project brief requires an editor that AI agents can drive without a human: build a Tetris-like game end
to end. ADR 0036 gave the editor one typed, validated, undoable command path with JSON Schemas. Agents and
tools now need a way to reach it from another process: the MCP bridge (M7k), `illusion_ctl`, scripts and CI.
That channel runs agent-written requests against a process that also runs agent-written game code, so it
must be safe by default, must not race with the frame loop, and must keep working when clients misbehave
(huge messages, floods, disconnects, wrong tokens).

## Decision

- **JSON-RPC 2.0, one object per line.** Messages are UTF-8 JSON objects separated by `\n` (NDJSON), at most
  16 MiB each. A longer line gets -32600 and closes the connection. Batch arrays are rejected with -32600;
  grouping comes from `editor.batch` (M7j-2), which knows about transactions. Ids may be strings, numbers or
  null; notifications never get a reply, not even an error. Protocol errors use the JSON-RPC codes
  (-32700, -32600, -32601 with `data.suggestions`, -32602 when `params` is not an object, -32603); command
  failures use the positive `ErrorKind` codes of ADR 0036 (1000–1500) with `data: {kind, errors,
  suggestions, retryable}`.
- **Two transports, no network.** `--rpc-stdio` serves the parent process over stdin and stdout (the log
  goes to stderr); the parent owns the pipes, so no authentication is needed. `--rpc-listen` serves an
  `AF_UNIX` stream socket (Windows 10 1803+ has them too) at `<project>/.illusion/run/editor.sock`, or at
  `illusion-<hash>.sock` in the per-user runtime directory when that path does not fit `sun_path` (103
  bytes) or, on Windows, is not ASCII. There is no TCP or HTTP transport in M7.
- **Endpoint file and token.** A listening editor writes `.illusion/run/editor-endpoint.json` (`pid`,
  `protocolVersion`, `editorVersion`, `transport`, `address`, `token`) atomically, and deletes it on exit;
  clients treat a file whose pid is gone as stale. The run directory, the socket and the file are restricted
  to the current user (mode 0700/0600, or a protected Windows access list granting only the current user).
  The token is 256 bits from `secureRandomBytes()`. A socket client's first message must be
  `rpc.authenticate {token, client}` within 2 s; the token is compared in constant time and never logged. A
  wrong token, a timeout or any other first message gets error 1000 and closes the connection.
- **Threads.** Each connection has a reader thread (framing, parsing, authentication, `rpc.discover`,
  unknown methods, permission checks and limits are answered there) and a writer thread with an outbox. Other
  requests go into one bounded queue that the main thread runs with `RpcServer::pump()` between frames, so
  commands never race with the World and the requests of a connection run and are answered in order.
  Headless loops block in `waitForWork()`; the windowed editor will pass a `wake` callback.
- **Limits.** 8 connections (more get 1401 and are closed), 64 queued requests per connection (1401,
  retryable), a queue of 256 (1002 busy, retryable), 1,024 events per pull subscription (then `overflow`),
  1,024 remembered `clientToken`s per client, and 64 MiB of unsent replies before a client that does not
  read is disconnected.
- **Permission modes.** `--permissions read-only|ask|auto`: read-only runs only read-only commands; ask runs
  Read and Edit commands and needs a person's approval for Destructive, Files, Build and System ones (a
  headless editor has nobody to ask, so it denies them with 1001); auto (the headless default) runs
  everything. Paths stay confined to project mounts, and `project.create`/`project.open` only reach
  directories inside the editor's workspace (`--workspace`). There is no command that runs a shell.
- **Idempotency and concurrency.** A mutating call with `clientToken` that repeats a remembered token gets the
  first reply again without running the command; the same token with another method is a validation error.
  `expectRevision` (ADR 0036) gives optimistic concurrency.
- **Events.** `editor.subscribe {topics, mode}` subscribes a connection: push mode sends
  `{"method":"event","params":{"topic","payload"}}` notifications as the editor flushes its events each
  frame; pull mode keeps them for `editor.poll_events`. `editor.unsubscribe` and `editor.quit` complete the
  server methods.
- **Discovery.** `rpc.discover` returns an OpenRPC 1.3 document of every command and server method (one
  content descriptor per parameter, the result schema, and `x-illusion` with version, permission, mode and
  flags), taken when the server starts.
- **Versioning.** The protocol is semver: `0.x` until the MCP bridge settles it, `1.0` at M7x. Commands keep
  their own versions (ADR 0036).
- **Process API.** `illusion::platform` gains `Process` (spawn without a shell through `CreateProcessW` with
  exact quoting and an explicit inherited-handle list, or `posix_spawn`; bounded stdio buffers drained by
  reader threads; `poll`, `wait(timeout)`, `terminate`), used by `illusion_ctl`, the replayer and later by
  `module.build`.

## Consequences

- One server serves `illusion_ctl`, the Python client (`tools/editor/illusion_rpc.py`), the acceptance
  runner (ADR 0038) and, in M7k, the MCP bridge; all of them get schemas, typed errors and suggestions.
- A misbehaving client cannot stall the frame loop: every limit answers instead of blocking, and a client
  that stops reading is disconnected.
- Requests are serialized on the main thread, so throughput is bounded by command cost (a cheap command costs
  about 1 µs per request when requests are pipelined on an in-memory connection; `rpc/roundtrip` allows 10 µs).
- `module.build` plus `module.load` (M7s) runs agent-written code; the documentation says so, and the ask mode
  will put a person in the loop once the windowed editor exists.
- Async commands with `rpc.progress`/`rpc.cancel`, `editor.batch` with `$` references, per-connection
  transactions and `scope: mine` undo follow in M7j-2 on top of this protocol.
- The method catalogue is a snapshot: commands registered later (hot-reloaded modules) need a server restart
  until `commands.changed` arrives with M7s.
