#include "TestSupport.hpp"

#include <illusion/core/Random.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <vector>

using namespace illusion;

static_assert(std::uniform_random_bit_generator<Random>);

TEST_CASE("Random produces the reference xoshiro256** sequence for seed 42")
{
    // Computed with an independent implementation of splitmix64 and xoshiro256** (Blackman and Vigna);
    // the sequence must be identical on every platform and compiler.
    constexpr std::array<u64, 16> kExpected{
        0x15780b2e0c2ec716ull, 0x6104d9866d113a7eull, 0xae17533239e499a1ull, 0xecb8ad4703b360a1ull,
        0xfde6dc7fe2ec5e64ull, 0xc50da53101795238ull, 0xb82154855a65ddb2ull, 0xd99a2743ebe60087ull,
        0xc2e96e726e97647eull, 0x9556615f775fbc3dull, 0xaeb53b340c103971ull, 0x4a69db9873af8965ull,
        0xcd0feda93006c6b6ull, 0x52480865a4b42742ull, 0xb60dec3bf2d887cdull, 0xe0b55a68b96677faull};
    Random random(42);
    for (const u64 expected : kExpected)
    {
        CHECK(random.next() == expected);
    }
}

TEST_CASE("Random seeds its state through splitmix64")
{
    // The first splitmix64 output for seed 0 is the published reference value 0xe220a8397b1dcdaf.
    const Random random(0);
    CHECK(random.state() == Random::State{0xe220a8397b1dcdafull, 0x6e789e6aa1b965f4ull, 0x06c45d188009454full,
                                          0xf88bb8a8724c81ecull});
    Random zero(0);
    CHECK(zero.next() == 0x99ec5f36cb75f2b4ull);
}

TEST_CASE("Random works in constant expressions")
{
    constexpr u64 first = [] {
        Random random(42);
        return random.next();
    }();
    static_assert(first == 0x15780b2e0c2ec716ull);
    CHECK(first == 0x15780b2e0c2ec716ull);
}

TEST_CASE("below() is deterministic and stays within bounds")
{
    Random dice(42);
    constexpr std::array<u64, 16> kExpected{0, 2, 4, 5, 5, 4, 4, 5, 4, 3, 4, 1, 4, 1, 4, 5};
    for (const u64 expected : kExpected)
    {
        CHECK(dice.below(6) == expected);
    }

    Random random(7);
    for (const u64 n : {u64{1}, u64{2}, u64{3}, u64{1000}, u64{1} << 63, ~u64{0}})
    {
        for (int i = 0; i < 1000; ++i)
        {
            CHECK(random.below(n) < n);
        }
    }
    CHECK(random.below(1) == 0);
}

TEST_CASE("below() is uniform by a chi-square test at a fixed seed")
{
    // 10 buckets, 100000 draws: the 0.999 quantile of chi-square with 9 degrees of freedom is 27.88.
    constexpr u64 kBuckets = 10;
    constexpr int kDraws = 100000;
    Random random(1234);
    std::array<int, kBuckets> counts{};
    for (int i = 0; i < kDraws; ++i)
    {
        ++counts[random.below(kBuckets)];
    }
    const f64 expected = static_cast<f64>(kDraws) / kBuckets;
    f64 chiSquare = 0.0;
    for (const int count : counts)
    {
        const f64 difference = count - expected;
        chiSquare += difference * difference / expected;
    }
    CHECK(chiSquare < 27.88);
}

TEST_CASE("range() covers both bounds and the full i64 range")
{
    Random random(99);
    bool sawMin = false;
    bool sawMax = false;
    for (int i = 0; i < 1000; ++i)
    {
        const i64 value = random.range(-2, 2);
        CHECK(value >= -2);
        CHECK(value <= 2);
        sawMin = sawMin || value == -2;
        sawMax = sawMax || value == 2;
    }
    CHECK(sawMin);
    CHECK(sawMax);
    CHECK(random.range(5, 5) == 5);
    (void)random.range(std::numeric_limits<i64>::min(), std::numeric_limits<i64>::max());
    const i64 high = random.range(std::numeric_limits<i64>::max() - 1, std::numeric_limits<i64>::max());
    CHECK(high >= std::numeric_limits<i64>::max() - 1);
}

TEST_CASE("uniform01() and uniformF32() stay between 0 and 1 and use the top bits")
{
    Random random(42);
    CHECK(random.uniform01() == 0x15780b2e0c2ec716ull / 2048 * 0x1.0p-53);
    for (int i = 0; i < 10000; ++i)
    {
        const f64 a = random.uniform01();
        const f32 b = random.uniformF32();
        CHECK(a >= 0.0);
        CHECK(a < 1.0);
        CHECK(b >= 0.0f);
        CHECK(b < 1.0f);
    }
}

TEST_CASE("chance() respects its extremes and boolean() is balanced")
{
    Random random(5);
    int heads = 0;
    for (int i = 0; i < 10000; ++i)
    {
        CHECK_FALSE(random.chance(0.0));
        CHECK(random.chance(1.0));
        heads += random.boolean() ? 1 : 0;
    }
    CHECK(heads > 4700);
    CHECK(heads < 5300);
}

TEST_CASE("state() and setState() resume a sequence")
{
    Random random(11);
    (void)random.next();
    const Random::State snapshot = random.state();
    const u64 a = random.next();
    const u64 b = random.next();
    Random other(999);
    other.setState(snapshot);
    CHECK(other.next() == a);
    CHECK(other.next() == b);
}

TEST_CASE("Random works with standard distributions and algorithms")
{
    Random random(3);
    std::vector<int> values{1, 2, 3, 4, 5, 6, 7, 8};
    std::shuffle(values.begin(), values.end(), random);
    std::sort(values.begin(), values.end());
    CHECK(values == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8});
    std::uniform_int_distribution<int> distribution(1, 3);
    const int value = distribution(random);
    CHECK(value >= 1);
    CHECK(value <= 3);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("Random asserts on empty ranges and invalid states")
{
    test::ScopedAssertCatcher catcher;
    Random random(1);
    CHECK_THROWS_AS((void)random.below(0), test::AssertionFailure);
    CHECK_THROWS_AS((void)random.range(2, 1), test::AssertionFailure);
    CHECK_THROWS_AS(random.setState(Random::State{}), test::AssertionFailure);
}
#endif

TEST_CASE("secureRandomBytes fills buffers with distinct bytes")
{
    std::array<std::byte, 64> a{};
    std::array<std::byte, 64> b{};
    REQUIRE(secureRandomBytes(a));
    REQUIRE(secureRandomBytes(b));
    CHECK(a != b); // a collision of 512 random bits does not happen
    // 64 random bytes are almost never all equal.
    CHECK(std::adjacent_find(a.begin(), a.end(), std::not_equal_to<>()) != a.end());
    CHECK(secureRandomBytes({}));

    std::vector<std::byte> large(1 << 20);
    REQUIRE(secureRandomBytes(large));
    const auto zeros = std::count(large.begin(), large.end(), std::byte{0});
    CHECK(zeros > 3500); // about 4096 expected
    CHECK(zeros < 4700);
}
