#pragma once

/// @file
/// Stable, platform-independent hashing.

#include <illusion/core/Base.hpp>

#include <string_view>

namespace illusion
{

/// 64-bit FNV-1a hash of @p text. Stable across platforms, compilers and runs, so it may be persisted.
[[nodiscard]] constexpr u64 fnv1a64(std::string_view text) noexcept
{
    u64 hash = 0xcbf29ce484222325ull;
    for (const char c : text)
    {
        hash ^= static_cast<u8>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

/// Mixes @p value into @p seed. Not stable across versions; never persist the result.
[[nodiscard]] constexpr u64 hashCombine(u64 seed, u64 value) noexcept
{
    // Based on the 64-bit finalizer of MurmurHash3.
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

} // namespace illusion
