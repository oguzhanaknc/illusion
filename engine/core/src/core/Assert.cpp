#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace illusion
{
namespace
{

bool isDebuggerAttached() noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    return IsDebuggerPresent() != 0;
#else
    // Breaking without a debugger would turn every assert into SIGTRAP; only abort() on POSIX.
    return false;
#endif
}

void defaultAssertHandler(const AssertInfo& info)
{
    Logger& logger = Logger::instance();
    if (info.message.empty())
    {
        logger.print(LogLevel::Fatal, "Assert", info.file, static_cast<u32>(info.line),
                     "assertion failed: {}", info.expression);
    }
    else
    {
        logger.print(LogLevel::Fatal, "Assert", info.file, static_cast<u32>(info.line),
                     "assertion failed: {} ({})", info.expression, info.message);
    }
    logger.flush();
    std::fprintf(stderr, "%s(%d): assertion failed: %s\n", info.file, info.line, info.expression);
    std::fflush(stderr);
    if (isDebuggerAttached())
    {
        ILL_DEBUG_BREAK();
    }
    std::abort();
}

std::atomic<AssertHandler> g_assertHandler{&defaultAssertHandler};

} // namespace

AssertHandler setAssertHandler(AssertHandler handler) noexcept
{
    return g_assertHandler.exchange(handler != nullptr ? handler : &defaultAssertHandler);
}

AssertHandler assertHandler() noexcept
{
    return g_assertHandler.load();
}

namespace detail
{

void assertFailed(const char* expression, const char* file, int line, std::string_view message)
{
    const AssertInfo info{expression, message, file, line};
    g_assertHandler.load()(info);
    // Handlers must not return; enforce it.
    std::abort();
}

} // namespace detail
} // namespace illusion
