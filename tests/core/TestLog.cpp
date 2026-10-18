#include "TestSupport.hpp"

#include <illusion/core/Log.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace illusion;

namespace
{

/// Routes the global logger to a private Logger with a MemorySink for the duration of a test.
struct ScopedTestLogger
{
    Logger logger;
    std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>(64);

    ScopedTestLogger()
    {
        logger.addSink(sink);
        logger.setLevel(LogLevel::Trace);
        Logger::setInstance(&logger);
    }
    ~ScopedTestLogger() { Logger::setInstance(nullptr); }
};

} // namespace

TEST_CASE("Log macros format messages and reach sinks")
{
    ScopedTestLogger scope;
    ILL_LOG_INFO("Test", "hello {} {}", "world", 42);
    ILL_LOG_ERROR("Render", "failed: {:.2f}", 1.5);

    const auto entries = scope.sink->entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].level == LogLevel::Info);
    CHECK(entries[0].category == "Test");
    CHECK(entries[0].text == "hello world 42");
    CHECK(entries[1].level == LogLevel::Error);
    CHECK(entries[1].text == "failed: 1.50");
}

TEST_CASE("Logger filters by level")
{
    ScopedTestLogger scope;
    scope.logger.setLevel(LogLevel::Warn);
    CHECK_FALSE(scope.logger.isEnabled(LogLevel::Info));
    CHECK(scope.logger.isEnabled(LogLevel::Error));
    ILL_LOG_INFO("Test", "dropped");
    ILL_LOG_WARN("Test", "kept");
    const auto entries = scope.sink->entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].text == "kept");

    scope.logger.setLevel(LogLevel::Off);
    CHECK_FALSE(scope.logger.isEnabled(LogLevel::Fatal));
}

TEST_CASE("Logger truncates overlong messages")
{
    ScopedTestLogger scope;
    const std::string longText(5000, 'x');
    ILL_LOG_INFO("Test", "{}", longText);
    const auto entries = scope.sink->entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].text.size() == 2048);
    CHECK(entries[0].text.ends_with("..."));
}

TEST_CASE("MemorySink keeps the newest messages in order")
{
    MemorySink sink(3);
    for (int i = 0; i < 5; ++i)
    {
        const std::string text = std::to_string(i);
        sink.write(LogMessage{LogLevel::Info, "C", text, nullptr, 0, 0, 0.0});
    }
    const auto entries = sink.entries();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].text == "2");
    CHECK(entries[1].text == "3");
    CHECK(entries[2].text == "4");
    sink.clear();
    CHECK(sink.entries().empty());
}

TEST_CASE("Logger can remove sinks")
{
    ScopedTestLogger scope;
    scope.logger.removeSink(scope.sink.get());
    ILL_LOG_INFO("Test", "nobody hears this");
    CHECK(scope.sink->entries().empty());
}

TEST_CASE("Logger is safe to use from many threads")
{
    ScopedTestLogger scope;
    auto sink = std::make_shared<MemorySink>(10000);
    scope.logger.addSink(sink);
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([t] {
            for (int i = 0; i < 100; ++i)
            {
                ILL_LOG_INFO("Thread", "thread {} message {}", t, i);
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    CHECK(sink->entries().size() == 800);
}

TEST_CASE("Messages logged from inside a sink are dropped instead of deadlocking")
{
    struct EchoSink final : LogSink
    {
        int writes = 0;
        void write(const LogMessage&) override
        {
            ++writes;
            ILL_LOG_WARN("Echo", "logging from a sink");
        }
    };
    ScopedTestLogger scope;
    auto echo = std::make_shared<EchoSink>();
    scope.logger.addSink(echo);
    ILL_LOG_INFO("Test", "outer");
    CHECK(echo->writes == 1);
    const auto entries = scope.sink->entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].text == "outer");
}

TEST_CASE("A sink may forward messages to a different Logger")
{
    struct ForwardSink final : LogSink
    {
        Logger* target = nullptr;
        void write(const LogMessage& message) override
        {
            target->write(message.level, "Forwarded", message.text);
        }
    };
    ScopedTestLogger scope;
    Logger other;
    auto otherSink = std::make_shared<MemorySink>(8);
    other.addSink(otherSink);
    auto forward = std::make_shared<ForwardSink>();
    forward->target = &other;
    scope.logger.addSink(forward);
    ILL_LOG_INFO("Test", "hello");
    const auto entries = otherSink->entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].category == "Forwarded");
    CHECK(entries[0].text == "hello");
}

TEST_CASE("logLevelName has fixed width")
{
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn,
                                 LogLevel::Error, LogLevel::Fatal, LogLevel::Off})
    {
        CHECK(logLevelName(level).size() == 5);
    }
}

// --- ConsoleSink ---------------------------------------------------------------------------------------

namespace
{

/// A temporary file used as a console stream.
struct TemporaryStream
{
    std::FILE* file = std::tmpfile();

    TemporaryStream() { REQUIRE(file != nullptr); }
    ~TemporaryStream()
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
    TemporaryStream(const TemporaryStream&) = delete;
    TemporaryStream& operator=(const TemporaryStream&) = delete;

    std::string contents() const
    {
        std::fflush(file);
        std::rewind(file);
        std::string text;
        char buffer[256];
        for (usize read = 0; (read = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
        {
            text.append(buffer, read);
        }
        return text;
    }
};

LogMessage message(LogLevel level, std::string_view text, std::string_view category = "Test",
                   const char* file = nullptr, u32 line = 0)
{
    return LogMessage{level, category, text, file, line, 3, 1.5};
}

} // namespace

TEST_CASE("ConsoleSink splits levels between output and error streams by default")
{
    TemporaryStream output;
    TemporaryStream error;
    ConsoleSink sink(ConsoleStreams::Split, output.file, error.file);
    CHECK(sink.streams() == ConsoleStreams::Split);
    sink.write(message(LogLevel::Info, "informational"));
    sink.write(message(LogLevel::Warn, "warning"));
    sink.flush();
    CHECK(output.contents() == "[    1.5000] [INFO ] [T3] [Test] informational\n");
    CHECK(error.contents() == "[    1.5000] [WARN ] [T3] [Test] warning\n");
    CHECK(ConsoleSink().streams() == ConsoleStreams::Split);
}

TEST_CASE("ConsoleSink with StderrOnly writes nothing to the output stream")
{
    TemporaryStream output;
    TemporaryStream error;
    ConsoleSink sink(ConsoleStreams::StderrOnly, output.file, error.file);
    for (const LogLevel level :
         {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Fatal})
    {
        sink.write(message(level, "text"));
    }
    sink.flush();
    CHECK(output.contents().empty());
    const std::string written = error.contents();
    CHECK(std::count(written.begin(), written.end(), '\n') == 6);
    CHECK(written.find("[INFO ]") != std::string::npos);

    // The output stream is never touched, so it may be null.
    ConsoleSink noOutput(ConsoleStreams::StderrOnly, nullptr, error.file);
    noOutput.write(message(LogLevel::Info, "still fine"));
    noOutput.flush();
    CHECK(error.contents().ends_with("still fine\n"));
}

// --- LogRingSink ---------------------------------------------------------------------------------------

TEST_CASE("LogRingSink stores records with increasing sequence numbers")
{
    LogRingSink sink(8);
    CHECK(sink.capacity() == 8);
    CHECK(sink.latestSequence() == 0);
    CHECK(sink.readSince(0, [](const LogRecord&) { FAIL("no records yet"); }) == 0);

    sink.write(message(LogLevel::Info, "first", "Core", "engine/core/src/Thing.cpp", 12));
    sink.write(message(LogLevel::Error, "second", "Render"));
    CHECK(sink.latestSequence() == 2);

    std::vector<std::string> texts;
    const u64 last = sink.readSince(0, [&](const LogRecord& record) {
        texts.emplace_back(record.textView());
        if (record.sequence == 1)
        {
            CHECK(record.level == LogLevel::Info);
            CHECK(record.categoryView() == "Core");
            CHECK(record.fileView() == "engine/core/src/Thing.cpp");
            CHECK(record.line == 12);
            CHECK(record.threadIndex == 3);
            CHECK(record.timeSeconds == 1.5);
            CHECK_FALSE(record.truncated);
        }
        else
        {
            CHECK(record.sequence == 2);
            CHECK(record.fileView().empty());
        }
    });
    CHECK(last == 2);
    CHECK(texts == std::vector<std::string>{"first", "second"});

    // Only newer records are visited.
    sink.write(message(LogLevel::Info, "third"));
    texts.clear();
    CHECK(sink.readSince(last, [&](const LogRecord& record) { texts.emplace_back(record.textView()); }) == 3);
    CHECK(texts == std::vector<std::string>{"third"});
    CHECK(sink.readSince(3, [](const LogRecord&) { FAIL("nothing new"); }) == 3);
    CHECK(sink.droppedCount() == 0);
}

namespace
{
int g_visitedByFunction = 0;
void countRecord(const LogRecord&)
{
    ++g_visitedByFunction;
}
} // namespace

TEST_CASE("LogRingSink readers can be functions and function pointers")
{
    LogRingSink sink(4);
    sink.write(message(LogLevel::Info, "one"));
    g_visitedByFunction = 0;
    CHECK(sink.readSince(0, countRecord) == 1);
    CHECK(sink.readSince(0, &countRecord) == 1);
    const auto visitor = [](const LogRecord& record) {
        CHECK(record.textView() == "one");
    };
    CHECK(sink.readSince(0, visitor) == 1); // a const lambda
    CHECK(g_visitedByFunction == 2);
}

TEST_CASE("LogRingSink counts records overwritten before anyone read them")
{
    LogRingSink sink(4);
    for (int i = 1; i <= 10; ++i)
    {
        sink.write(message(LogLevel::Info, std::to_string(i)));
    }
    CHECK(sink.droppedCount() == 6); // 1..6 were overwritten unread

    std::vector<u64> sequences;
    CHECK(sink.readSince(0, [&](const LogRecord& record) { sequences.push_back(record.sequence); }) == 10);
    CHECK(sequences == std::vector<u64>{7, 8, 9, 10});

    // Overwriting records that were read is not a drop.
    for (int i = 11; i <= 14; ++i)
    {
        sink.write(message(LogLevel::Info, std::to_string(i)));
    }
    CHECK(sink.droppedCount() == 6);
    sink.write(message(LogLevel::Info, "15")); // overwrites 11, which nobody read
    CHECK(sink.droppedCount() == 7);

    // A cursor at or past a record counts as having read it (12..15 here), but a cursor ahead of the newest
    // record does not mark records written later as read.
    CHECK(sink.readSince(100, [](const LogRecord&) { FAIL("nothing after 100"); }) == 100);
    for (int i = 16; i <= 20; ++i)
    {
        sink.write(message(LogLevel::Info, std::to_string(i)));
    }
    CHECK(sink.droppedCount() == 8); // 16 was overwritten unread by 20
}

TEST_CASE("LogRingSink cuts long strings at UTF-8 character boundaries")
{
    LogRingSink sink(2);
    // "\xC3\xA7" (two bytes) straddles the text limit; "\xE2\x82\xAC" (three bytes) straddles the category
    // limit; the file keeps its end, starting after a cut character.
    const std::string text = std::string(LogRecord::kMaxTextBytes - 1, 'a') + "\xC3\xA7" + "tail";
    const std::string category = std::string(LogRecord::kMaxCategoryBytes - 1, 'c') + "\xE2\x82\xAC";
    const std::string file = "\xE2\x82\xAC" + std::string(LogRecord::kMaxFileBytes - 2, 'f') + "/Name.cpp";
    sink.write(message(LogLevel::Info, text, category, file.c_str(), 7));
    sink.write(message(LogLevel::Info, "short"));
    (void)sink.readSince(0, [&](const LogRecord& record) {
        if (record.sequence != 1)
        {
            CHECK_FALSE(record.truncated);
            return;
        }
        CHECK(record.truncated);
        CHECK(record.textView() == std::string(LogRecord::kMaxTextBytes - 1, 'a'));
        CHECK(record.categoryView() == std::string(LogRecord::kMaxCategoryBytes - 1, 'c'));
        CHECK(record.fileView().ends_with("/Name.cpp"));
        CHECK(record.fileView().size() <= LogRecord::kMaxFileBytes);
        CHECK(static_cast<u8>(record.fileView()[0]) == 'f');
    });
}

TEST_CASE("LogRingSink does not allocate after construction")
{
    LogRingSink sink(16);
    const std::string text(4000, 'x');
    test::ScopedAllocationCounter allocations;
    for (int i = 0; i < 100; ++i)
    {
        sink.write(message(LogLevel::Warn, text, "Category", __FILE__, 1));
    }
    usize visited = 0;
    (void)sink.readSince(0, [&](const LogRecord&) { ++visited; });
    CHECK(visited == 16);
    CHECK(allocations.count() == 0);
}

TEST_CASE("LogRingSink receives messages through a Logger")
{
    ScopedTestLogger scope;
    auto ring = std::make_shared<LogRingSink>(32);
    scope.logger.addSink(ring);
    ILL_LOG_WARN("Ring", "value {}", 42);
    bool found = false;
    (void)ring->readSince(0, [&](const LogRecord& record) {
        found = true;
        CHECK(record.textView() == "value 42");
        CHECK(record.categoryView() == "Ring");
        CHECK(record.fileView().ends_with("TestLog.cpp"));
        CHECK(record.line > 0);
    });
    CHECK(found);
}

TEST_CASE("LogRingSink is safe with concurrent writers and a reader")
{
    constexpr int kWriters = 8;
    constexpr int kMessages = 2000;
    LogRingSink sink(256);
    std::atomic<int> finished{0};

    u64 visited = 0;
    u64 cursor = 0;
    bool ordered = true;
    std::thread reader([&] {
        const auto read = [&] {
            u64 previous = cursor;
            cursor = sink.readSince(cursor, [&](const LogRecord& record) {
                // Sequences only grow: consecutive within a call, past the cursor across calls.
                ordered = ordered && record.sequence > previous &&
                          (previous == cursor || record.sequence == previous + 1);
                previous = record.sequence;
                ++visited;
            });
        };
        while (finished.load(std::memory_order_acquire) < kWriters)
        {
            read();
            std::this_thread::yield();
        }
        read();
    });

    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w)
    {
        writers.emplace_back([&sink, &finished, w] {
            char text[32];
            for (int i = 0; i < kMessages; ++i)
            {
                const int length = std::snprintf(text, sizeof(text), "writer %d message %d", w, i);
                sink.write(LogMessage{LogLevel::Info, "Thread",
                                      std::string_view(text, static_cast<usize>(length)), nullptr, 0,
                                      static_cast<u32>(w), 0.0});
            }
            finished.fetch_add(1, std::memory_order_release);
        });
    }
    for (std::thread& writer : writers)
    {
        writer.join();
    }
    reader.join();

    CHECK(ordered);
    CHECK(sink.latestSequence() == u64{kWriters} * kMessages);
    CHECK(cursor == sink.latestSequence());
    // Every record was either read or counted as dropped.
    CHECK(visited + sink.droppedCount() == sink.latestSequence());
}
