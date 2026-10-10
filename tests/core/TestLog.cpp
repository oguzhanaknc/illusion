#include <illusion/core/Log.hpp>

#include <doctest/doctest.h>

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
