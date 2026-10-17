#pragma once

/// @file
/// Assertion macros.
///
/// - `ILL_ASSERT(cond, [fmt, args...])` — programmer-error check. Compiled in Debug builds, and in every
///   configuration when the `ILLUSION_ENABLE_ASSERTS` CMake option is ON (CI does this). The condition
///   is not evaluated when asserts are disabled, so it must not have side effects.
/// - `ILL_VERIFY(cond, [fmt, args...])` — like ILL_ASSERT but the condition is always evaluated.
/// - `ILL_CHECK(cond, [fmt, args...])` — always-on check for unrecoverable runtime conditions.
/// - `ILL_UNREACHABLE()` — marks code that must never execute.
///
/// A failed assertion calls the installed AssertHandler and then aborts the process. Handlers may throw
/// (tests use this to check that an assertion fires) but must never simply return.

#include <illusion/core/Base.hpp>

#include <format>
#include <string_view>
#include <utility>

namespace illusion
{

/// Information about a failed assertion.
struct AssertInfo
{
    const char* expression;   ///< Stringified condition that failed.
    std::string_view message; ///< Optional formatted message (may be empty).
    const char* file;         ///< Source file of the assertion.
    int line;                 ///< Source line of the assertion.
};

/// Called when an assertion fails. Must not return normally (it may throw or terminate).
using AssertHandler = void (*)(const AssertInfo& info);

/// Installs @p handler (nullptr restores the default) and returns the previous handler.
/// The default handler logs a Fatal message, breaks into an attached debugger and aborts.
AssertHandler setAssertHandler(AssertHandler handler) noexcept;

/// Returns the currently installed assert handler (never nullptr).
[[nodiscard]] AssertHandler assertHandler() noexcept;

/// Implementation details. Not part of the public API.
namespace detail
{

[[noreturn]] void assertFailed(const char* expression, const char* file, int line, std::string_view message);

/// Calls the handler installed in this binary at the time of the call. Modules use the host's copy as
/// their handler, so that later setAssertHandler() calls in the host reach them too.
[[noreturn]] void dispatchAssertion(const AssertInfo& info);

[[noreturn]] inline void assertFailed(const char* expression, const char* file, int line)
{
    assertFailed(expression, file, line, std::string_view{});
}

template <class... Args>
[[noreturn]] ILL_NOINLINE void assertFailed(const char* expression, const char* file, int line,
                                            std::format_string<Args...> fmt, Args&&... args)
{
    char buffer[1024];
    const auto result = std::format_to_n(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
    const usize length = static_cast<usize>(result.out - buffer);
    assertFailed(expression, file, line, std::string_view(buffer, length));
}

} // namespace detail
} // namespace illusion

/// Always-on check. Calls the assert handler and aborts if @p cond is false.
#define ILL_CHECK(cond, ...)                                                                                 \
    do                                                                                                       \
    {                                                                                                        \
        if (!(cond)) [[unlikely]]                                                                            \
        {                                                                                                    \
            ::illusion::detail::assertFailed(#cond, __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__);          \
        }                                                                                                    \
    } while (false)

#if defined(ILLUSION_ENABLE_ASSERTS) && ILLUSION_ENABLE_ASSERTS
/// 1 when ILL_ASSERT is active in this build, 0 otherwise.
#define ILL_ASSERTS_ENABLED 1
/// Debug-only check; see the file documentation.
#define ILL_ASSERT(cond, ...) ILL_CHECK(cond __VA_OPT__(, ) __VA_ARGS__)
/// Debug-only check whose condition is always evaluated.
#define ILL_VERIFY(cond, ...) ILL_CHECK(cond __VA_OPT__(, ) __VA_ARGS__)
#else
#define ILL_ASSERTS_ENABLED 0
#define ILL_ASSERT(cond, ...)                                                                                \
    do                                                                                                       \
    {                                                                                                        \
        (void)sizeof(!(cond));                                                                               \
    } while (false)
#define ILL_VERIFY(cond, ...)                                                                                \
    do                                                                                                       \
    {                                                                                                        \
        (void)(cond);                                                                                        \
    } while (false)
#endif

/// Marks code that must never be reached.
#define ILL_UNREACHABLE() ::illusion::detail::assertFailed("unreachable", __FILE__, __LINE__)
