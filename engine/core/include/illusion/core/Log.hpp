#pragma once

/// @file
/// Thread-safe logging with pluggable sinks.
///
/// Use the `ILL_LOG_*` macros: `ILL_LOG_INFO("Render", "created {} buffers", count);`
/// Messages are formatted into a thread-local buffer, so logging itself does not allocate (sinks may).
/// Trace and Debug messages are compiled out when NDEBUG is defined.

#include <illusion/core/Base.hpp>

#include <cstddef>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace illusion
{

/// Severity of a log message, from most to least verbose.
enum class LogLevel : u8
{
    Trace, ///< Very detailed diagnostics.
    Debug, ///< Diagnostics useful while developing.
    Info,  ///< Normal operational messages.
    Warn,  ///< Something unexpected that the engine recovered from.
    Error, ///< An operation failed.
    Fatal, ///< The engine cannot continue.
    Off    ///< Only valid as a filter level: disables all output.
};

/// Returns a fixed-width (5 character) name for @p level, e.g. "INFO ".
[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;

/// A single log record passed to sinks. The views are only valid during LogSink::write().
struct LogMessage
{
    LogLevel level;            ///< Severity.
    std::string_view category; ///< Subsystem name, e.g. "ECS" or "Render".
    std::string_view text;     ///< Formatted message text.
    const char* file;          ///< Source file (may be nullptr).
    u32 line;                  ///< Source line (0 if unknown).
    u32 threadIndex;           ///< Logical thread index (see currentThreadIndex()).
    f64 timeSeconds;           ///< Seconds since the logger was created.
};

/// Destination for log messages. Sinks are called with the logger lock held, one message at a time.
/// Messages a sink logs to its own Logger (including through failed assertions) are dropped.
class LogSink
{
public:
    virtual ~LogSink() = default;

    /// Writes one message.
    virtual void write(const LogMessage& message) = 0;

    /// Flushes buffered output. Called after Error and Fatal messages and by Logger::flush().
    virtual void flush() {}
};

/// Writes messages to stdout (Trace..Info) and stderr (Warn..Fatal).
class ConsoleSink final : public LogSink
{
public:
    /// @copydoc LogSink::write
    void write(const LogMessage& message) override;
    /// @copydoc LogSink::flush
    void flush() override;
};

/// Keeps the most recent messages in memory. Used by tests and the editor console.
class MemorySink final : public LogSink
{
public:
    /// A stored copy of a LogMessage.
    struct Entry
    {
        LogLevel level;       ///< Severity.
        std::string category; ///< Subsystem name.
        std::string text;     ///< Formatted message text.
    };

    /// Creates a sink that keeps at most @p capacity messages (the oldest are dropped first).
    explicit MemorySink(usize capacity = 1024);

    /// @copydoc LogSink::write
    void write(const LogMessage& message) override;

    /// Returns a copy of the stored messages, oldest first.
    [[nodiscard]] std::vector<Entry> entries() const;

    /// Removes all stored messages.
    void clear();

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
    usize m_capacity;
    usize m_next = 0;
};

/// Process-wide logger. Hot-reloaded modules receive the host's instance via Logger::setInstance().
class Logger : NonCopyable
{
public:
    /// Creates a logger with no sinks and level Info.
    Logger();
    ~Logger();

    /// Returns the active logger. A built-in default with a ConsoleSink is used until one is set.
    [[nodiscard]] static Logger& instance() noexcept;

    /// Makes @p logger the active instance (nullptr restores the built-in default). The caller keeps
    /// ownership and must keep @p logger alive while it is active.
    static void setInstance(Logger* logger) noexcept;

    /// Adds a sink. The logger shares ownership.
    void addSink(std::shared_ptr<LogSink> sink);

    /// Removes a previously added sink. Does nothing if @p sink is not registered.
    void removeSink(const LogSink* sink);

    /// Removes all sinks.
    void clearSinks();

    /// Messages below @p level are discarded.
    void setLevel(LogLevel level) noexcept;

    /// Returns the current minimum level.
    [[nodiscard]] LogLevel level() const noexcept;

    /// Returns true if a message of @p level would be written.
    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;

    /// Writes an already formatted message to all sinks.
    void write(LogLevel level, std::string_view category, std::string_view text, const char* file = nullptr,
               u32 line = 0);

    /// Formats a message into a thread-local buffer (no allocation) and writes it. Messages longer
    /// than the buffer are truncated and end with "...".
    template <class... Args>
    void print(LogLevel level, std::string_view category, const char* file, u32 line,
               std::format_string<Args...> fmt, Args&&... args)
    {
        if (!isEnabled(level))
        {
            return;
        }
        char* buffer = formatBuffer();
        const auto result = std::format_to_n(buffer, kFormatBufferSize, fmt, std::forward<Args>(args)...);
        write(level, category, finishFormat(buffer, result.size), file, line);
    }

    /// Flushes all sinks.
    void flush();

private:
    static constexpr std::ptrdiff_t kFormatBufferSize = 2048;
    static char* formatBuffer() noexcept;
    static std::string_view finishFormat(char* buffer, std::ptrdiff_t formattedSize) noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

/// Logs a formatted message at @p level through the active Logger.
#define ILL_LOG(level, category, ...)                                                                        \
    ::illusion::Logger::instance().print(level, category, __FILE__, static_cast<::illusion::u32>(__LINE__),  \
                                         __VA_ARGS__)

#if defined(NDEBUG)
#define ILL_LOG_TRACE(category, ...) ((void)0)
#define ILL_LOG_DEBUG(category, ...) ((void)0)
#else
/// Logs at LogLevel::Trace (compiled out when NDEBUG is defined).
#define ILL_LOG_TRACE(category, ...) ILL_LOG(::illusion::LogLevel::Trace, category, __VA_ARGS__)
/// Logs at LogLevel::Debug (compiled out when NDEBUG is defined).
#define ILL_LOG_DEBUG(category, ...) ILL_LOG(::illusion::LogLevel::Debug, category, __VA_ARGS__)
#endif
/// Logs at LogLevel::Info.
#define ILL_LOG_INFO(category, ...) ILL_LOG(::illusion::LogLevel::Info, category, __VA_ARGS__)
/// Logs at LogLevel::Warn.
#define ILL_LOG_WARN(category, ...) ILL_LOG(::illusion::LogLevel::Warn, category, __VA_ARGS__)
/// Logs at LogLevel::Error.
#define ILL_LOG_ERROR(category, ...) ILL_LOG(::illusion::LogLevel::Error, category, __VA_ARGS__)
/// Logs at LogLevel::Fatal.
#define ILL_LOG_FATAL(category, ...) ILL_LOG(::illusion::LogLevel::Fatal, category, __VA_ARGS__)
