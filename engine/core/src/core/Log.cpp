#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Thread.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace illusion
{

std::string_view logLevelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO ";
    case LogLevel::Warn:
        return "WARN ";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    case LogLevel::Off:
        return "OFF  ";
    }
    return "?????";
}

// --- ConsoleSink --------------------------------------------------------------------------------------

void ConsoleSink::write(const LogMessage& message)
{
    std::FILE* stream = message.level >= LogLevel::Warn ? stderr : stdout;
    const std::string_view level = logLevelName(message.level);
    std::fprintf(stream, "[%10.4f] [%.*s] [T%u] [%.*s] %.*s\n", message.timeSeconds,
                 static_cast<int>(level.size()), level.data(), message.threadIndex,
                 static_cast<int>(message.category.size()), message.category.data(),
                 static_cast<int>(message.text.size()), message.text.data());
}

void ConsoleSink::flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}

// --- MemorySink ---------------------------------------------------------------------------------------

MemorySink::MemorySink(usize capacity)
    : m_capacity(std::max<usize>(capacity, 1))
{
}

void MemorySink::write(const LogMessage& message)
{
    std::scoped_lock lock(m_mutex);
    Entry entry{message.level, std::string(message.category), std::string(message.text)};
    if (m_entries.size() < m_capacity)
    {
        m_entries.push_back(std::move(entry));
    }
    else
    {
        m_entries[m_next] = std::move(entry);
    }
    m_next = (m_next + 1) % m_capacity;
}

std::vector<MemorySink::Entry> MemorySink::entries() const
{
    std::scoped_lock lock(m_mutex);
    if (m_entries.size() < m_capacity)
    {
        return m_entries;
    }
    // Ring buffer is full: the oldest entry is at m_next.
    std::vector<Entry> ordered;
    ordered.reserve(m_entries.size());
    for (usize i = 0; i < m_entries.size(); ++i)
    {
        ordered.push_back(m_entries[(m_next + i) % m_capacity]);
    }
    return ordered;
}

void MemorySink::clear()
{
    std::scoped_lock lock(m_mutex);
    m_entries.clear();
    m_next = 0;
}

// --- Logger -------------------------------------------------------------------------------------------

struct Logger::Impl
{
    std::mutex mutex;
    std::vector<std::shared_ptr<LogSink>> sinks;
    std::atomic<LogLevel> level{LogLevel::Info};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // Thread currently inside write() with the mutex held; used to detect re-entrant logging.
    std::atomic<std::thread::id> writer{};
};

namespace
{

Logger& defaultLogger()
{
    static Logger logger;
    static const bool initialized = [] {
        logger.addSink(std::make_shared<ConsoleSink>());
        return true;
    }();
    (void)initialized;
    return logger;
}

std::atomic<Logger*> g_logger{nullptr};

} // namespace

Logger::Logger()
    : m_impl(std::make_unique<Impl>())
{
}

Logger::~Logger() = default;

Logger& Logger::instance() noexcept
{
    Logger* logger = g_logger.load(std::memory_order_acquire);
    return logger != nullptr ? *logger : defaultLogger();
}

void Logger::setInstance(Logger* logger) noexcept
{
    g_logger.store(logger, std::memory_order_release);
}

void Logger::addSink(std::shared_ptr<LogSink> sink)
{
    ILL_CHECK(sink != nullptr);
    std::scoped_lock lock(m_impl->mutex);
    m_impl->sinks.push_back(std::move(sink));
}

void Logger::removeSink(const LogSink* sink)
{
    std::scoped_lock lock(m_impl->mutex);
    std::erase_if(m_impl->sinks, [sink](const std::shared_ptr<LogSink>& s) { return s.get() == sink; });
}

void Logger::clearSinks()
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->sinks.clear();
}

void Logger::setLevel(LogLevel level) noexcept
{
    m_impl->level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept
{
    return m_impl->level.load(std::memory_order_relaxed);
}

bool Logger::isEnabled(LogLevel level) const noexcept
{
    return level != LogLevel::Off && level >= m_impl->level.load(std::memory_order_relaxed);
}

void Logger::write(LogLevel level, std::string_view category, std::string_view text, const char* file,
                   u32 line)
{
    if (!isEnabled(level))
    {
        return;
    }
    // A sink of this logger that logs to it again (directly, or through a failed assertion) would
    // deadlock on the non-recursive mutex. Drop such re-entrant messages; the assert handler also prints
    // to stderr. Only this thread can have stored its own id, so a relaxed load is sufficient.
    const std::thread::id self = std::this_thread::get_id();
    if (m_impl->writer.load(std::memory_order_relaxed) == self)
    {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - m_impl->start;
    const LogMessage message{
        level, category, text, file, line, currentThreadIndex(), std::chrono::duration<f64>(elapsed).count()};

    std::scoped_lock lock(m_impl->mutex);
    m_impl->writer.store(self, std::memory_order_relaxed);
    struct ClearWriter
    {
        std::atomic<std::thread::id>& writer;
        ~ClearWriter() { writer.store(std::thread::id{}, std::memory_order_relaxed); }
    } clearWriter{m_impl->writer};
    for (const auto& sink : m_impl->sinks)
    {
        sink->write(message);
        if (level >= LogLevel::Error)
        {
            sink->flush();
        }
    }
}

void Logger::flush()
{
    std::scoped_lock lock(m_impl->mutex);
    for (const auto& sink : m_impl->sinks)
    {
        sink->flush();
    }
}

char* Logger::formatBuffer() noexcept
{
    thread_local char buffer[kFormatBufferSize];
    return buffer;
}

std::string_view Logger::finishFormat(char* buffer, std::ptrdiff_t formattedSize) noexcept
{
    if (formattedSize <= kFormatBufferSize)
    {
        return {buffer, static_cast<usize>(formattedSize)};
    }
    // Truncated: mark the end so readers know text is missing.
    std::memcpy(buffer + kFormatBufferSize - 3, "...", 3);
    return {buffer, static_cast<usize>(kFormatBufferSize)};
}

} // namespace illusion
