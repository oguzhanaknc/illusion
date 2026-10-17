#include <illusion/core/Thread.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace illusion
{
namespace
{

std::atomic<u32> g_nextThreadIndex{0};
std::atomic<u32 (*)() noexcept> g_threadIndexSource{nullptr};

} // namespace

u32 currentThreadIndex() noexcept
{
    if (u32(*source)() noexcept = g_threadIndexSource.load(std::memory_order_relaxed))
    {
        return source();
    }
    thread_local const u32 index = g_nextThreadIndex.fetch_add(1, std::memory_order_relaxed);
    return index;
}

void detail::setThreadIndexSource(u32 (*source)() noexcept) noexcept
{
    // The host's own function never forwards, so installing it into the host is harmless.
    g_threadIndexSource.store(source == &currentThreadIndex ? nullptr : source, std::memory_order_relaxed);
}

void setCurrentThreadName(const char* name) noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    wchar_t wide[64] = {};
    const int written = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 63);
    if (written > 0)
    {
        SetThreadDescription(GetCurrentThread(), wide);
    }
#elif defined(ILL_PLATFORM_MACOS)
    pthread_setname_np(name);
#else
    // Linux limits names to 16 bytes including the terminator.
    char truncated[16] = {};
    std::memcpy(truncated, name, std::min(std::strlen(name), sizeof(truncated) - 1));
    pthread_setname_np(pthread_self(), truncated);
#endif
}

u32 hardwareThreadCount() noexcept
{
    const unsigned count = std::thread::hardware_concurrency();
    return count == 0 ? 1u : static_cast<u32>(count);
}

} // namespace illusion
