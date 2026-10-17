#include <illusion/core/Hash.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

TEST_CASE("fnv1a64 matches the reference test vectors")
{
    // Reference values from the FNV specification.
    CHECK(fnv1a64("") == 0xcbf29ce484222325ull);
    CHECK(fnv1a64("a") == 0xaf63dc4c8601ec8cull);
    CHECK(fnv1a64("foobar") == 0x85944171f73967e8ull);
}

TEST_CASE("fnv1a64 is usable at compile time")
{
    static_assert(fnv1a64("illusion") == fnv1a64("illusion"));
    static_assert(fnv1a64("Position") != fnv1a64("Velocity"));
}

TEST_CASE("hashCombine depends on order and on both inputs")
{
    CHECK(hashCombine(1, 2) != hashCombine(2, 1));
    CHECK(hashCombine(1, 2) != hashCombine(1, 3));
    CHECK(hashCombine(1, 2) != hashCombine(4, 2));
}

TEST_CASE("xxhash64 matches the reference implementation")
{
    const auto hash = [](std::string_view text, u64 seed = 0) {
        return xxhash64(std::as_bytes(std::span(text.data(), text.size())), seed);
    };
    // Reference values of XXH64 (seed 0); the last input covers the 32-byte stripes and every tail path.
    CHECK(hash("") == 0xEF46DB3751D8E999ull);
    CHECK(hash("abc") == 0x44BC2CF5AD770999ull);
    CHECK(hash("Nobody inspects the spammish repetition") == 0xFBCEA83C8A378BF1ull);
    CHECK(hash("abc", 1) != hash("abc"));
    // Every length from 0 to 100 gives a distinct value (exercises all tail combinations).
    std::string text;
    std::vector<u64> seen;
    for (int length = 0; length <= 100; ++length)
    {
        const u64 value = hash(text);
        CHECK(std::find(seen.begin(), seen.end(), value) == seen.end());
        seen.push_back(value);
        text.push_back(static_cast<char>('a' + length % 26));
    }
}
