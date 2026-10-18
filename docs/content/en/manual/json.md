# JSON and Expected

illusion reads and writes JSON for scene files, project settings, tool protocols (JSON-RPC, MCP) and
string tables. The JSON API never throws: every failure is a return value. It keeps the parser private,
so no third-party header reaches your code.

Headers: `#include <illusion/core/Json.hpp>` and `#include <illusion/core/Expected.hpp>`

## Expected

C++20 has no `std::expected`, so the core library provides `Expected<T, E>` for operations that can
fail in a recoverable way. It holds either a value of type `T` or an error of type `E`:

```cpp
illusion::Expected<illusion::u16, std::string> parsePort(std::string_view text)
{
    illusion::u16 port = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc() || end != text.data() + text.size())
    {
        return illusion::unexpected(std::format("'{}' is not a port number", text));
    }
    return port;
}

const auto port = parsePort(argument);
if (!port)
{
    ILL_LOG_ERROR("Net", "{}", port.error());
    return;
}
connect(*port);
```

| Member | Meaning |
| --- | --- |
| `hasValue()`, `explicit operator bool` | True if a value is held. |
| `value()`, `operator*`, `operator->` | The value. Asserts if an error is held. |
| `error()` | The error. Asserts if a value is held. |
| `valueOr(fallback)` | The value, or `fallback` for an error. |

`Expected<void, E>` reports success without a value. `Expected` is nothrow-movable when `T` and `E` are,
and works in `constexpr` code. Use it, `bool` or `std::optional` for recoverable failures; engine code
never throws exceptions.

## Reading JSON

`JsonDocument::parse()` returns the document, or a `JsonError` with the byte offset, the line, the
column and a message:

```cpp
auto parsed = illusion::JsonDocument::parse(text);
if (!parsed)
{
    const illusion::JsonError& error = parsed.error();
    ILL_LOG_ERROR("Project", "project.json:{}:{}: {}", error.line, error.column, error.message);
    return;
}
const illusion::JsonValue root = parsed->root();
const std::string_view name = root.find("name").asString().value_or("Untitled");
const illusion::i64 width = root.pointer("/window/size/0").asI64().value_or(1280);
for (const illusion::JsonValue scene : root.find("scenes").elements())
{
    // ...
}
for (const illusion::JsonMember member : root.find("settings").members())
{
    // member.key, member.value
}
```

The reader is strict. It rejects:

- anything that is not standard JSON (RFC 8259): comments, trailing commas, single quotes, NaN and
  Infinity, leading zeros, a byte order mark;
- invalid UTF-8, and content after the document;
- duplicate keys in an object (the error points at the repeated key);
- nesting deeper than `maxDepth` (128 by default) and input larger than `maxBytes` (64 MiB). Both are
  checked before the document is built, so oversized or overly deep input costs at most one pass over
  its text.

Change the limits with `JsonReadOptions`. `allowDuplicateKeys = true` accepts repeated keys; lookups
then return the first one.

```cpp
illusion::JsonReadOptions options;
options.maxDepth = 512;
options.maxBytes = 256 * 1024 * 1024;
auto large = illusion::JsonDocument::parse(text, options);
```

## Values

A `JsonValue` is a cheap view into a document: it is valid as long as the document lives (moving the
document keeps it valid). A missing value is **absent** instead of an error, so lookups chain without
checks in between: `root.find("a").find("b")[3]` is absent if any step is missing.

| Function | Result |
| --- | --- |
| `type()`, `isNull()`, `isBool()`, `isInteger()`, `isReal()`, `isNumber()`, `isString()`, `isArray()`, `isObject()`, `isAbsent()` | The kind of value. |
| `asBool()`, `asI64()`, `asU64()`, `asF64()`, `asString()` | The content as `std::optional`; empty if the type does not match or an integer is out of range. `asF64()` also accepts integers. |
| `size()` | Elements of an array or members of an object. |
| `operator[](index)`, `find(key)` | An element or member, or an absent value. |
| `elements()`, `members()` | Ranges in file order. |
| `pointer("/a/0")` | A JSON Pointer (RFC 6901): `~0` is `~`, `~1` is `/`; malformed pointers are absent. |
| `dump()` | Compact text for diagnostics. |

Integers keep their exact value in the full `i64` and `u64` range, also above 2^53, where `f64`
loses precision.

## Writing JSON

`JsonMutDocument` builds a document. Strings and keys are copied:

```cpp
illusion::JsonMutDocument doc;
illusion::JsonMutValue root = doc.makeObject();
doc.setRoot(root);
root.add("name", doc.makeString("Crate"));
root.add("mass", doc.makeF32(12.5f));
illusion::JsonMutValue size = root.addArray("size");
size.push(doc.makeF32(1.0f)).push(doc.makeF32(2.0f)).push(doc.makeF32(1.0f));

std::string text;
if (const auto written = doc.write(text); !written)
{
    ILL_LOG_ERROR("Tools", "{}", written.error().message);
}
```

`add()` appends a member and expects a new key; `set()` replaces an existing one; `remove()` deletes it.
`copy()` makes a deep copy of a `JsonValue` from a parsed document, so a document can be read, edited
and written back. A value belongs to the document that made it: adding it to another document asserts. `makeF32()` writes the shortest text that reads back as the same `f32` (`0.1`, not
`0.10000000149011612`).

`write(out, options)` replaces the contents of `out`. Writing fails for non-finite numbers and strings
that are not valid UTF-8. `JsonValue::write()` writes parsed values the same way.

| `JsonWriteOptions` | Effect |
| --- | --- |
| `pretty = true` (default) | Canonical layout: two-space indentation, arrays of scalars on one line, a final newline. |
| `pretty = false` | Compact: no whitespace at all. |
| `sortKeys = true` | Object members ordered by the bytes of their keys, at every level. |

Pretty output is the format of scene files. Numbers use the shortest text that reads back to the same
value, so documents round-trip bit for bit:

```json
{
  "name": "Crate",
  "mass": 12.5,
  "size": [1.0, 2.0, 1.0]
}
```

## Memory

Parsing and building can take their memory from a `LinearAllocator` instead of the heap:

```cpp
illusion::LinearAllocator arena(1024 * 1024);
auto parsed = illusion::JsonDocument::parse(text, arena);

illusion::JsonMutDocument reply(arena);
```

A parsed document keeps a copy of the text and 16 bytes for every value (key, scalar, array, object).
Typical documents need 3 to 5 times the size of the text, and arrays of one-digit numbers up to 11
times. When the arena is full, `parse()` fails with "out of memory". A `JsonMutDocument` instead returns
absent values, remembers the failure (`failed()`), and `write()` reports it, so a builder needs only one
check at the end.

Do not use a document or its values after the arena was reset or rewound below it. Destroying or
assigning to such a document is fine: it never touches arena memory, so a frame arena may be reset
before the documents built in it go out of scope.

Writing reuses the capacity of the output string and keeps its working memory on the stack, so writing
into the same string again does not touch the heap. Only documents nested more than 64 levels deep, and
with `sortKeys` more than 128 members in the objects being written at once, need a heap allocation.

## Errors and threading

`JsonError::offset`, `line` and `column` (1-based, counted in bytes) point at the problem in the input.
Write errors have only a message. A `JsonMutDocument` is not thread-safe; any number of threads may read
the same `JsonDocument` at once.

See [ADR 0028](../architecture/decisions.md) for the design.
