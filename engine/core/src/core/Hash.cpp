#include <illusion/core/Hash.hpp>

#include <bit>
#include <cstring>

namespace illusion
{
namespace
{

// XXH64 as specified in https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md.
constexpr u64 kPrime1 = 0x9E3779B185EBCA87ull;
constexpr u64 kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr u64 kPrime3 = 0x165667B19E3779F9ull;
constexpr u64 kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr u64 kPrime5 = 0x27D4EB2F165667C5ull;

u64 read64(const std::byte* p) noexcept
{
    u64 value = 0;
    std::memcpy(&value, p, sizeof(value)); // little-endian hosts only (see Serialization.hpp)
    return value;
}

u32 read32(const std::byte* p) noexcept
{
    u32 value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

u64 round(u64 accumulator, u64 lane) noexcept
{
    accumulator += lane * kPrime2;
    accumulator = std::rotl(accumulator, 31);
    return accumulator * kPrime1;
}

u64 mergeRound(u64 hash, u64 accumulator) noexcept
{
    hash ^= round(0, accumulator);
    return hash * kPrime1 + kPrime4;
}

} // namespace

u64 xxhash64(std::span<const std::byte> bytes, u64 seed) noexcept
{
    const std::byte* p = bytes.data();
    const std::byte* const end = p + bytes.size();
    u64 hash = 0;
    if (bytes.size() >= 32)
    {
        u64 v1 = seed + kPrime1 + kPrime2;
        u64 v2 = seed + kPrime2;
        u64 v3 = seed;
        u64 v4 = seed - kPrime1;
        const std::byte* const limit = end - 32;
        do
        {
            v1 = round(v1, read64(p));
            v2 = round(v2, read64(p + 8));
            v3 = round(v3, read64(p + 16));
            v4 = round(v4, read64(p + 24));
            p += 32;
        } while (p <= limit);
        hash = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = mergeRound(hash, v1);
        hash = mergeRound(hash, v2);
        hash = mergeRound(hash, v3);
        hash = mergeRound(hash, v4);
    }
    else
    {
        hash = seed + kPrime5;
    }
    hash += static_cast<u64>(bytes.size());
    while (end - p >= 8)
    {
        hash ^= round(0, read64(p));
        hash = std::rotl(hash, 27) * kPrime1 + kPrime4;
        p += 8;
    }
    if (end - p >= 4)
    {
        hash ^= static_cast<u64>(read32(p)) * kPrime1;
        hash = std::rotl(hash, 23) * kPrime2 + kPrime3;
        p += 4;
    }
    while (p < end)
    {
        hash ^= static_cast<u64>(static_cast<u8>(*p)) * kPrime5;
        hash = std::rotl(hash, 11) * kPrime1;
        ++p;
    }
    hash ^= hash >> 33;
    hash *= kPrime2;
    hash ^= hash >> 29;
    hash *= kPrime3;
    hash ^= hash >> 32;
    return hash;
}

} // namespace illusion
