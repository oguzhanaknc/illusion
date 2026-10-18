#pragma once

// Helpers shared by the text tests.

#include <illusion/core/Log.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace illusion::test
{

/// Routes the global logger to a private Logger with a MemorySink while it is alive.
struct ScopedMemoryLogger
{
    Logger logger;
    std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>(64);

    explicit ScopedMemoryLogger(LogLevel level = LogLevel::Info)
    {
        logger.addSink(sink);
        logger.setLevel(level);
        Logger::setInstance(&logger);
    }
    ~ScopedMemoryLogger() { Logger::setInstance(nullptr); }
    ScopedMemoryLogger(const ScopedMemoryLogger&) = delete;
    ScopedMemoryLogger& operator=(const ScopedMemoryLogger&) = delete;
};

/// Contents of tests/data/<relativePath>; fails the test if the file cannot be read.
inline std::string readDataFile(std::string_view relativePath)
{
    // The data directory is a UTF-8 path (it may contain non-ASCII characters).
    const std::string path = std::string(ILLUSION_TEST_DATA_DIR) + "/" + std::string(relativePath);
    std::ifstream stream(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())),
        std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "cannot open ", path);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return std::move(contents).str();
}

/// Compiles @p pattern, failing the test if it does not compile.
inline text::MessageFormat compiled(std::string_view pattern)
{
    auto message = text::MessageFormat::compile(pattern);
    INFO(pattern);
    REQUIRE_MESSAGE(message.hasValue(), (message ? std::string() : message.error().message));
    return std::move(message).value();
}

/// Formats @p message with @p arguments and @p context into a new string.
inline std::string formatted(const text::MessageFormat& message,
                             std::initializer_list<text::MessageArgument> arguments,
                             const text::MessageContext& context = {})
{
    std::string out;
    text::TextOutput output(out);
    (void)message.format(arguments, context, output);
    return out;
}

/// Compiles and formats @p pattern.
inline std::string formatted(std::string_view pattern, std::initializer_list<text::MessageArgument> arguments,
                             const text::MessageContext& context = {})
{
    return formatted(compiled(pattern), arguments, context);
}

} // namespace illusion::test
