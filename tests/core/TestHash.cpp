#include <illusion/core/Hash.hpp>

#include <doctest/doctest.h>

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
