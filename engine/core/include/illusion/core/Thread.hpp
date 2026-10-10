#pragma once

/// @file
/// Thread utilities: logical thread indices, thread naming and spin-wait hints.

#include <illusion/core/Base.hpp>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(_M_ARM64)
#include <intrin.h>
#endif

namespace illusion
{

/// Returns a small, stable index for the calling thread. The first thread to call this gets 0,
/// the next 1, and so on. Indices are never reused within a process.
[[nodiscard]] u32 currentThreadIndex() noexcept;

/// Sets the calling thread's name as shown by debuggers, profilers and crash reports.
/// Names longer than 15 characters are truncated on Linux.
void setCurrentThreadName(const char* name) noexcept;

/// Returns the number of hardware threads (at least 1).
[[nodiscard]] u32 hardwareThreadCount() noexcept;

/// Hints to the CPU that the caller is in a spin-wait loop.
ILL_FORCEINLINE void cpuRelax() noexcept
{
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(_M_ARM64)
    __yield();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

} // namespace illusion
