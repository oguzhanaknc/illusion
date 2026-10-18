# 0028. Offer a non-throwing public JSON API over private yyjson, and Expected for recoverable errors

- Status: Accepted
- Date: 2026-10-18

## Context

Until M6, JSON was used in one place: the scene serializer, which calls yyjson directly (ADR 0022). The
editor milestone (M7) needs JSON everywhere: the reflection codec, scene format v2, project files, the
command journal, the JSON-RPC server, the MCP bridge, string tables and themes. Each of those must report
bad input precisely (an agent or a user wrote it), must never crash or throw, and some run on paths where
allocation matters (a JSON-RPC reply per frame, log tails). Scene files must keep their current, diffable
format.

The engine has no common type for "a value or an error": APIs return `bool` plus an out parameter, a
result struct with an `error` string (`SceneLoadResult`), or `std::optional`. C++20 has no
`std::expected`.

Options considered for JSON:

- **Expose yyjson in public headers.** Fast and complete, but every caller would depend on a C API with
  manual null checks and lifetime rules, the dependency could not be replaced, and duplicate keys and depth
  limits would have to be checked by each caller.
- **nlohmann/json.** Convenient, but it reports errors with exceptions (forbidden in engine code), allocates
  per value, and compiles slowly.
- **simdjson.** The fastest reader, but it only reads (no DOM to build or write), has its own error-code
  style per access, and is large.
- **An in-house parser.** Full control, but a correct, fast, fuzzed UTF-8 and number parser is a large
  project; yyjson already provides one under the MIT licence.

## Decision

- **Expected** (`illusion/core/Expected.hpp`): `Expected<T, E>` and `Expected<void, E>` with
  `Unexpected<E>`/`unexpected()`, `hasValue()`, an explicit `operator bool`, `value()`/`error()` that
  `ILL_ASSERT` on the wrong alternative, `operator*`/`->` and `valueOr()`. Header-only, `constexpr`,
  nothrow-movable when `T` and `E` are. New APIs with recoverable failures that carry details return it;
  existing result structs stay until their APIs are revised.
- **JSON** (`illusion/core/Json.hpp`, in `illusion::core`): a public API whose implementation is yyjson,
  which stays a private dependency; the header only forward-declares yyjson's struct types.
  - `JsonDocument::parse()` returns `Expected<JsonDocument, JsonError>`; `JsonError` has the byte offset,
    the 1-based line and byte column, and an English message. `JsonValue` is a one-pointer view with typed
    `std::optional` getters, `find()`, indexing, file-order ranges and an in-house RFC 6901 `pointer()`;
    missing values are absent views, so lookups chain without checks.
  - **Strict input.** Only RFC 8259 JSON is accepted (yyjson is built with `YYJSON_DISABLE_NON_STANDARD`):
    no comments, trailing commas, NaN or Infinity, invalid UTF-8 or trailing content. `JsonReadOptions`
    limits nesting (`maxDepth`, 128) and size (`maxBytes`, 64 MiB); the depth is checked on yyjson's flat
    value array without recursion, so hostile input cannot overflow the stack.
  - **Duplicate keys are rejected** by default, at the position of the repeated key, because JSON leaves
    their meaning undefined and parsers disagree (first wins, last wins, error). Files and protocol
    messages therefore mean the same to every tool. `allowDuplicateKeys` opts out; lookups then return the
    first occurrence.
  - Integers keep their exact `i64`/`u64` value; only numbers outside both ranges become reals.
  - `JsonMutDocument`/`JsonMutValue` build documents (strings and keys copied, deep copies from parsed
    values). Handles stay valid when the document moves.
- **Canonical formatting.** Pretty output is the scene file format: two-space indentation, arrays of
  scalars on one line, the shortest text that reads back to the same number (`f32` values in their `f32`
  form), yyjson's string escaping, and a final newline. Compact output has no whitespace. `sortKeys`
  orders members by key bytes at every level, for content hashes and stable diffs. The writer is
  iterative and fails, without throwing, on non-finite numbers and invalid UTF-8. Tests check that it
  reproduces `saveScene()` output byte for byte, so M7e can rebuild the serializer on it.
- **Arena allocation.** Parsing and building accept a caller's `LinearAllocator`, plugged into yyjson
  through `yyjson_alc`; `LinearAllocator::reallocate()` grows the newest block in place so yyjson's
  `realloc` works on an arena. A full arena is a recoverable error: `parse()` returns "out of memory", and
  a mutable document records the failure, returns absent handles and reports it from `write()`. Writing
  reuses the output string's capacity and keeps its stacks inline, so steady-state writing does not
  allocate. Heap exhaustion stays fatal (`ILL_CHECK`).

## Consequences

- Tools get one JSON vocabulary with precise errors; the editor and protocol code never handle yyjson
  types, and yyjson could be replaced behind the header.
- Lookups by key are linear in the number of members, like yyjson's; code that looks up many keys in a
  large object should iterate `members()` once instead.
- Strictness may reject files that lenient tools accept (comments in hand-written settings). If that
  becomes a problem, a separate, explicit option will be added rather than relaxing the default.
- Integers above 2^53 must be read with `asI64()`/`asU64()`; reading them with `asF64()` rounds.
- The scene serializer still calls yyjson directly until M7e moves it onto this API.
