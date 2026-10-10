#pragma once

/// @file
/// Fundamental types, platform detection and compiler helpers used by every illusion module.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define ILL_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#define ILL_PLATFORM_MACOS 1
#elif defined(__linux__)
#define ILL_PLATFORM_LINUX 1
#else
#error "Unsupported platform"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define ILL_COMPILER_MSVC 1
#elif defined(__clang__)
#define ILL_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define ILL_COMPILER_GCC 1
#endif

#if defined(_MSC_VER)
#define ILL_FORCEINLINE __forceinline
#define ILL_NOINLINE __declspec(noinline)
#define ILL_DEBUG_BREAK() __debugbreak()
#else
/// Forces inlining of a function (use only on tiny, hot functions).
#define ILL_FORCEINLINE inline __attribute__((always_inline))
/// Prevents inlining of a function (use for cold paths such as error reporting).
#define ILL_NOINLINE __attribute__((noinline))
/// Stops in the debugger (or terminates when no debugger is attached).
#define ILL_DEBUG_BREAK() __builtin_trap()
#endif

/// Stringify a macro argument after expansion.
#define ILL_STRINGIFY(x) ILL_STRINGIFY_IMPL(x)
/// Concatenate two tokens after expansion.
#define ILL_CONCAT(a, b) ILL_CONCAT_IMPL(a, b)

/// @cond INTERNAL
#define ILL_STRINGIFY_IMPL(x) #x
#define ILL_CONCAT_IMPL(a, b) a##b
/// @endcond

/// Root namespace of the engine.
namespace illusion
{

using u8 = std::uint8_t;   ///< 8-bit unsigned integer.
using u16 = std::uint16_t; ///< 16-bit unsigned integer.
using u32 = std::uint32_t; ///< 32-bit unsigned integer.
using u64 = std::uint64_t; ///< 64-bit unsigned integer.
using i8 = std::int8_t;    ///< 8-bit signed integer.
using i16 = std::int16_t;  ///< 16-bit signed integer.
using i32 = std::int32_t;  ///< 32-bit signed integer.
using i64 = std::int64_t;  ///< 64-bit signed integer.
using f32 = float;         ///< 32-bit IEEE-754 float.
using f64 = double;        ///< 64-bit IEEE-754 float.
using usize = std::size_t; ///< Unsigned size type.

/// Size of a CPU cache line. Used to pad data shared between threads and avoid false sharing.
inline constexpr usize kCacheLineSize = 64;

/// Returns true if @p value is a non-zero power of two.
[[nodiscard]] constexpr bool isPowerOfTwo(usize value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

/// Rounds @p value up to the next multiple of @p alignment (which must be a power of two).
[[nodiscard]] constexpr usize alignUp(usize value, usize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/// Base class that disables copy and move. Use for types with identity (systems, worlds, allocators).
class NonCopyable
{
public:
    NonCopyable(const NonCopyable&) = delete;            ///< Not copyable.
    NonCopyable& operator=(const NonCopyable&) = delete; ///< Not copyable.
    NonCopyable(NonCopyable&&) = delete;                 ///< Not movable.
    NonCopyable& operator=(NonCopyable&&) = delete;      ///< Not movable.

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
};

} // namespace illusion
