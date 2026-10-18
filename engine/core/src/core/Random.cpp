#include <illusion/core/Random.hpp>

#include <algorithm>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
// bcrypt.h uses types from windows.h without including it.
#include <bcrypt.h>
#elif defined(ILL_PLATFORM_LINUX)
#include <cerrno>
#include <sys/random.h>
#elif defined(ILL_PLATFORM_MACOS)
#include <stdlib.h>
#endif

namespace illusion
{
namespace
{

// Fills @p data with @p size bytes from the operating system's secure source.
bool systemRandom(std::byte* data, usize size) noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    // BCryptGenRandom takes a ULONG length, so large requests are split.
    while (size > 0)
    {
        const auto chunk = static_cast<ULONG>(std::min<usize>(size, 0x7FFFFFFFu));
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(data), chunk,
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            return false;
        }
        data += chunk;
        size -= chunk;
    }
    return true;
#elif defined(ILL_PLATFORM_LINUX)
    // getrandom() may return fewer bytes than requested (large requests, signals) or fail with EINTR.
    while (size > 0)
    {
        const ssize_t read = getrandom(data, size, 0);
        if (read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        data += read;
        size -= static_cast<usize>(read);
    }
    return true;
#elif defined(ILL_PLATFORM_MACOS)
    // arc4random_buf() cannot fail.
    arc4random_buf(data, size);
    return true;
#endif
}

} // namespace

bool secureRandomBytes(std::span<std::byte> out) noexcept
{
    return out.empty() || systemRandom(out.data(), out.size());
}

} // namespace illusion
