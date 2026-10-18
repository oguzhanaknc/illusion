#pragma once

/// @file
/// Random numbers: a fast deterministic generator for gameplay and tools, and secure bytes from the OS.
///
/// ```cpp
/// Random random(42);                     // same sequence on every platform and compiler
/// const u64 card = random.below(52);     // unbiased, in [0, 52)
/// const i64 roll = random.range(1, 6);   // inclusive
/// const bool crit = random.chance(0.15); // true with probability 0.15
///
/// std::array<std::byte, 16> token{};
/// const bool ok = secureRandomBytes(token); // for ids and keys; never for gameplay
/// ```
///
/// Random is xoshiro256** (Blackman and Vigna) seeded through splitmix64. It is not cryptographically
/// secure: use secureRandomBytes() for anything an attacker must not predict.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <span>

namespace illusion
{

/// Implementation details. Not part of the public API.
namespace detail
{

// Returns the high 64 bits of a * b and stores the low 64 bits in @p low. Built from 32-bit halves
// because standard C++ has no 128-bit integer.
[[nodiscard]] constexpr u64 multiplyHigh(u64 a, u64 b, u64& low) noexcept
{
    const u64 aLow = a & 0xFFFFFFFFull;
    const u64 aHigh = a >> 32;
    const u64 bLow = b & 0xFFFFFFFFull;
    const u64 bHigh = b >> 32;
    const u64 lowLow = aLow * bLow;
    const u64 highLow = aHigh * bLow;
    const u64 lowHigh = aLow * bHigh;
    const u64 highHigh = aHigh * bHigh;
    const u64 middle = (lowLow >> 32) + (highLow & 0xFFFFFFFFull) + lowHigh;
    low = (middle << 32) | (lowLow & 0xFFFFFFFFull);
    return highHigh + (highLow >> 32) + (middle >> 32);
}

} // namespace detail

/// Deterministic pseudo-random generator (xoshiro256**). Produces the same sequence for the same seed on
/// every platform and compiler, so it can drive replays, procedural content and tests. Satisfies
/// std::uniform_random_bit_generator. Not thread-safe: use one generator per thread or job.
class Random
{
public:
    /// Type of the values returned by next() (std::uniform_random_bit_generator).
    using result_type = u64;

    /// Complete generator state, for snapshots (see state() and setState()).
    using State = std::array<u64, 4>;

    /// Creates a generator seeded with @p seed (see seed()).
    constexpr explicit Random(u64 seed = 0) noexcept { this->seed(seed); }

    /// Restarts the sequence from @p seed. Every seed, including 0, gives a valid, distinct sequence: the
    /// state is filled with four outputs of splitmix64 started at @p seed.
    constexpr void seed(u64 seed) noexcept
    {
        u64 x = seed;
        for (u64& word : m_state)
        {
            x += 0x9e3779b97f4a7c15ull;
            u64 z = x;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            word = z ^ (z >> 31);
        }
    }

    /// Returns the next 64 random bits.
    constexpr u64 next() noexcept
    {
        const u64 result = std::rotl(m_state[1] * 5, 7) * 9;
        const u64 t = m_state[1] << 17;
        m_state[2] ^= m_state[0];
        m_state[3] ^= m_state[1];
        m_state[1] ^= m_state[2];
        m_state[0] ^= m_state[3];
        m_state[2] ^= t;
        m_state[3] = std::rotl(m_state[3], 45);
        return result;
    }

    /// Same as next() (std::uniform_random_bit_generator).
    constexpr u64 operator()() noexcept { return next(); }

    /// Smallest value next() returns.
    [[nodiscard]] static constexpr u64 min() noexcept { return 0; }

    /// Largest value next() returns.
    [[nodiscard]] static constexpr u64 max() noexcept { return ~u64{0}; }

    /// Returns a uniformly distributed value in [0, @p n). @p n must be greater than 0. Unbiased: uses
    /// Lemire's multiply-and-shift with rejection, which almost never needs a second draw.
    constexpr u64 below(u64 n)
    {
        ILL_ASSERT(n > 0, "Random::below(0)");
        u64 low = 0;
        u64 high = detail::multiplyHigh(next(), n, low);
        if (low < n)
        {
            const u64 threshold = (0 - n) % n; // 2^64 mod n
            while (low < threshold)
            {
                high = detail::multiplyHigh(next(), n, low);
            }
        }
        return high;
    }

    /// Returns a uniformly distributed value in [@p min, @p max] (both inclusive). @p min must not be
    /// greater than @p max.
    constexpr i64 range(i64 min, i64 max)
    {
        ILL_ASSERT(min <= max, "Random::range({}, {}): min is greater than max", min, max);
        const u64 span = static_cast<u64>(max) - static_cast<u64>(min);
        const u64 offset = span == ~u64{0} ? next() : below(span + 1);
        return static_cast<i64>(static_cast<u64>(min) + offset);
    }

    /// Returns a uniformly distributed double in [0, 1) built from the top 53 bits of next().
    constexpr f64 uniform01() noexcept { return static_cast<f64>(next() >> 11) * 0x1.0p-53; }

    /// Returns a uniformly distributed float in [0, 1) built from the top 24 bits of next().
    constexpr f32 uniformF32() noexcept { return static_cast<f32>(next() >> 40) * 0x1.0p-24f; }

    /// Returns true or false with equal probability (the top bit of next()).
    constexpr bool boolean() noexcept { return (next() >> 63) != 0; }

    /// Returns true with probability @p probability. Values at or below 0 never and values at or above 1
    /// always return true; one value of the sequence is consumed either way.
    constexpr bool chance(f64 probability) noexcept { return uniform01() < probability; }

    /// The current state. Restoring it with setState() continues the sequence from this point.
    [[nodiscard]] constexpr State state() const noexcept { return m_state; }

    /// Restores a state returned by state(). An all-zero state is invalid (the generator would only
    /// return zeros) and asserts.
    constexpr void setState(const State& state)
    {
        ILL_ASSERT((state[0] | state[1] | state[2] | state[3]) != 0,
                   "Random::setState() with an all-zero state");
        m_state = state;
    }

private:
    State m_state{};
};

/// Fills @p out with cryptographically secure random bytes from the operating system (BCryptGenRandom on
/// Windows, getrandom on Linux, arc4random_buf on Apple platforms). Returns false if the system source
/// failed; @p out is then unspecified and must not be used. Blocks only while the OS entropy pool is
/// being initialized early during boot. Thread-safe.
[[nodiscard]] bool secureRandomBytes(std::span<std::byte> out) noexcept;

} // namespace illusion
