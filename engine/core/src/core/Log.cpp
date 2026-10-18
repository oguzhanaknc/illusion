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

ConsoleSink::ConsoleSink(ConsoleStreams streams) noexcept
    : ConsoleSink(streams, stdout, stderr)
{
}

ConsoleSink::ConsoleSink(ConsoleStreams streams, std::FILE* output, std::FILE* error)
    : m_streams(streams)
    , m_output(output)
    , m_error(error)
{
    ILL_ASSERT(error != nullptr, "ConsoleSink needs an error stream");
    ILL_ASSERT(output != nullptr || streams == ConsoleStreams::StderrOnly,
               "ConsoleSink needs an output stream unless it writes to the error stream only");
}

void ConsoleSink::write(const LogMessage& message)
{
    const bool toError = m_streams == ConsoleStreams::StderrOnly || message.level >= LogLevel::Warn;
    std::FILE* stream = toError ? m_error : m_output;
    const std::string_view level = logLevelName(message.level);
    std::fprintf(stream, "[%10.4f] [%.*s] [T%u] [%.*s] %.*s\n", message.timeSeconds,
                 static_cast<int>(level.size()), level.data(), message.threadIndex,
                 static_cast<int>(message.category.size()), message.category.data(),
                 static_cast<int>(message.text.size()), message.text.data());
}

void ConsoleSink::flush()
{
    if (m_streams == ConsoleStreams::Split)
    {
        std::fflush(m_output);
    }
    std::fflush(m_error);
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

// --- LogRingSink -------------------------------------------------------------------------------------

namespace
{

static_assert(sizeof(LogRecord) == 1024, "keep LogRecord at a power-of-two size");

constexpr bool isContinuationByte(char c) noexcept
{
    return (static_cast<u8>(c) & 0xC0) == 0x80;
}

// A UTF-8 character has at most three continuation bytes. Longer runs are invalid UTF-8 and are cut
// anywhere rather than searched for a boundary that does not exist.
constexpr usize kMaxContinuationBytes = 3;

// Longest prefix of @p text that fits in @p capacity bytes without splitting a UTF-8 character.
std::string_view utf8Prefix(std::string_view text, usize capacity) noexcept
{
    if (text.size() <= capacity)
    {
        return text;
    }
    // Back up over the continuation bytes of a character cut in the middle.
    usize length = capacity;
    while (length > 0 && capacity - length < kMaxContinuationBytes && isContinuationByte(text[length]))
    {
        --length;
    }
    return text.substr(0, isContinuationByte(text[length]) ? capacity : length);
}

// Longest suffix of @p text that fits in @p capacity bytes and starts at a UTF-8 character boundary.
std::string_view utf8Suffix(std::string_view text, usize capacity) noexcept
{
    if (text.size() <= capacity)
    {
        return text;
    }
    // Skip the continuation bytes of a character cut in the middle.
    const usize cut = text.size() - capacity;
    usize start = cut;
    while (start - cut < kMaxContinuationBytes && start < text.size() && isContinuationByte(text[start]))
    {
        ++start;
    }
    return text.substr(start < text.size() && isContinuationByte(text[start]) ? cut : start);
}

} // namespace

LogRingSink::LogRingSink(usize capacity)
    : m_records(std::make_unique<LogRecord[]>(std::max<usize>(capacity, 1)))
    , m_capacity(std::max<usize>(capacity, 1))
{
}

LogRingSink::~LogRingSink() = default;

void LogRingSink::write(const LogMessage& message)
{
    const std::string_view category = utf8Prefix(message.category, LogRecord::kMaxCategoryBytes);
    const std::string_view text = utf8Prefix(message.text, LogRecord::kMaxTextBytes);
    const std::string_view file =
        message.file != nullptr ? utf8Suffix(message.file, LogRecord::kMaxFileBytes) : std::string_view{};

    std::scoped_lock lock(m_mutex);
    const u64 sequence = m_latest + 1;
    LogRecord& record = m_records[static_cast<usize>((sequence - 1) % m_capacity)];
    if (record.sequence != 0 && record.sequence > m_readUpTo)
    {
        ++m_dropped; // overwritten before any reader got past it
    }
    record.sequence = sequence;
    record.timeSeconds = message.timeSeconds;
    record.line = message.line;
    record.threadIndex = message.threadIndex;
    record.level = message.level;
    record.truncated = text.size() < message.text.size();
    record.categoryLength = static_cast<u8>(category.size());
    record.fileLength = static_cast<u8>(file.size());
    record.textLength = static_cast<u16>(text.size());
    // copy_n, unlike memcpy, accepts the null data of an empty view.
    std::copy_n(category.data(), category.size(), record.category);
    std::copy_n(file.data(), file.size(), record.file);
    std::copy_n(text.data(), text.size(), record.text);
    m_latest = sequence;
}

u64 LogRingSink::readSinceImpl(u64 after, RawVisitor visitor, void* context) const
{
    // Records are copied out in small batches and visited without the lock: a visitor may log (also to a
    // Logger this sink belongs to, which would otherwise deadlock against writers holding that Logger's
    // lock), assert, or read this sink again, and writers only wait for the copies.
    constexpr usize kBatchSize = 8;
    LogRecord batch[kBatchSize];
    u64 last = after;
    u64 end = 0; // newest record when the call started; later records wait for the next call
    bool first = true;
    for (;;)
    {
        usize count = 0;
        {
            std::scoped_lock lock(m_mutex);
            if (first)
            {
                end = m_latest;
                first = false;
            }
            const u64 oldest = m_latest >= m_capacity ? m_latest - m_capacity + 1 : 1;
            // Records between `last` and `oldest` were overwritten and counted as dropped.
            for (u64 sequence = last < end ? std::max(last + 1, oldest) : end + 1;
                 sequence <= end && count < kBatchSize; ++sequence)
            {
                batch[count++] = m_records[static_cast<usize>((sequence - 1) % m_capacity)];
            }
            if (count > 0)
            {
                last = batch[count - 1].sequence;
            }
            // A cursor beyond the newest record does not mark future records as read.
            m_readUpTo = std::max(m_readUpTo, std::min(last, m_latest));
        }
        for (usize i = 0; i < count; ++i)
        {
            visitor(context, batch[i]);
        }
        if (count < kBatchSize || last >= end)
        {
            return last;
        }
    }
}

u64 LogRingSink::latestSequence() const
{
    std::scoped_lock lock(m_mutex);
    return m_latest;
}

u64 LogRingSink::droppedCount() const
{
    std::scoped_lock lock(m_mutex);
    return m_dropped;
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
