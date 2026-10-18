# Logging

Header: `#include <illusion/core/Log.hpp>`

## Writing messages

Use the logging macros with a category and a `std::format` string:

```cpp
ILL_LOG_INFO("Render", "created {} buffers ({:.1f} MiB)", count, megabytes);
ILL_LOG_WARN("Assets", "texture '{}' is not a power of two", path);
ILL_LOG_ERROR("Audio", "device lost");
```

| Level | Macro | Use for |
| --- | --- | --- |
| Trace | `ILL_LOG_TRACE` | Very detailed diagnostics (removed from Release builds). |
| Debug | `ILL_LOG_DEBUG` | Development diagnostics (removed from Release builds). |
| Info | `ILL_LOG_INFO` | Normal operation. |
| Warn | `ILL_LOG_WARN` | Unexpected situations the engine recovered from. |
| Error | `ILL_LOG_ERROR` | Failed operations. |
| Fatal | `ILL_LOG_FATAL` | Unrecoverable errors. |

Format strings are checked at compile time. Messages are formatted into a thread-local buffer, so
logging itself does not allocate; messages longer than 2048 characters are truncated and end with
`...`.

## Filtering

```cpp
illusion::Logger::instance().setLevel(illusion::LogLevel::Warn);
```

## Sinks

A Sink receives every message that passes the filter. The default Logger writes to the console. Add
your own Sink by deriving from `LogSink`:

```cpp
class FileSink final : public illusion::LogSink
{
public:
    explicit FileSink(const char* path) : m_file(std::fopen(path, "w")) {}
    ~FileSink() override { std::fclose(m_file); }

    void write(const illusion::LogMessage& message) override
    {
        std::fprintf(m_file, "[%s] %.*s\n", illusion::logLevelName(message.level).data(),
                     static_cast<int>(message.text.size()), message.text.data());
    }
    void flush() override { std::fflush(m_file); }

private:
    std::FILE* m_file;
};

illusion::Logger::instance().addSink(std::make_shared<FileSink>("game.log"));
```

`MemorySink` keeps the most recent messages in memory; tests use it.

### Console streams

`ConsoleSink` writes Trace, Debug and Info to stdout and Warn, Error and Fatal to stderr. A process that
speaks a protocol on stdout, such as a JSON-RPC or MCP server, must keep stdout clean; give it a
console that writes every level to stderr:

```cpp
illusion::Logger& logger = illusion::Logger::instance();
logger.clearSinks();
logger.addSink(std::make_shared<illusion::ConsoleSink>(illusion::ConsoleStreams::StderrOnly));
```

Tests can pass their own `FILE*` streams: `ConsoleSink(ConsoleStreams::Split, output, error)`.

### Ring sink

`LogRingSink` keeps the last messages in a fixed ring of records for consoles, log tails and tools that
poll. It reserves all its memory when it is created, so logging to it never allocates, and it is safe
to read while other threads write. Every record gets a sequence number; a reader remembers the last one
it has seen and asks for newer records:

```cpp
auto ring = std::make_shared<illusion::LogRingSink>(4096);
illusion::Logger::instance().addSink(ring);

illusion::u64 cursor = 0;
void showNewMessages()
{
    cursor = ring->readSince(cursor, [](const illusion::LogRecord& record) {
        printLine(record.sequence, record.level, record.categoryView(), record.textView());
    });
}
```

The callback runs while the sink's lock is held: keep it short, do not log from it, and copy what you
need to keep. A record stores bounded copies of the category (32 bytes), the end of the source file path
(96 bytes) and the text (864 bytes), cut at a UTF-8 character boundary; `truncated` tells when the text
was cut. Because the strings are copies, records stay valid after the game module that logged them is
unloaded.

When writers are faster than readers, the oldest records are overwritten. `droppedCount()` counts the
records that were overwritten before any `readSince()` call had read past them; `latestSequence()` is
the sequence of the newest record.

## Threading

The Logger is thread-safe. Sinks are called one message at a time while the Logger's lock is held, so
a Sink does not need its own locking but must be quick and must not log itself.

## Assertions

Header: `#include <illusion/core/Assert.hpp>`

| Macro | Behaviour |
| --- | --- |
| `ILL_ASSERT(cond, ...)` | Checked in Debug (and everywhere with `ILLUSION_ENABLE_ASSERTS`). The condition must not have side effects. |
| `ILL_VERIFY(cond, ...)` | Like `ILL_ASSERT`, but the condition is always evaluated. |
| `ILL_CHECK(cond, ...)` | Always checked, for unrecoverable runtime errors. |
| `ILL_UNREACHABLE()` | Marks code that must never run. |

An optional format string and arguments describe the failure: `ILL_ASSERT(index < size, "index {}", index)`.
A failed check logs a Fatal message and aborts. Tests install a handler with `setAssertHandler()` that
throws instead.
