#include "TestSupport.hpp"

#include <illusion/core/InplaceFunction.hpp>

#include <doctest/doctest.h>

#include <memory>

using namespace illusion;

TEST_CASE("InplaceFunction stores and invokes callables")
{
    InplaceFunction<int(int, int)> add = [](int a, int b) {
        return a + b;
    };
    REQUIRE(static_cast<bool>(add));
    CHECK(add(2, 3) == 5);

    int captured = 10;
    InplaceFunction<int(int)> addCaptured = [&captured](int a) {
        return a + captured;
    };
    captured = 20;
    CHECK(addCaptured(1) == 21);
}

TEST_CASE("InplaceFunction is empty by default and after reset")
{
    InplaceFunction<void()> fn;
    CHECK_FALSE(static_cast<bool>(fn));
    fn = [] {
    };
    CHECK(static_cast<bool>(fn));
    fn.reset();
    CHECK_FALSE(static_cast<bool>(fn));
    InplaceFunction<void()> null = nullptr;
    CHECK_FALSE(static_cast<bool>(null));
}

TEST_CASE("InplaceFunction supports move-only captures and moves ownership")
{
    auto value = std::make_unique<int>(42);
    InplaceFunction<int()> a = [p = std::move(value)] {
        return *p;
    };
    InplaceFunction<int()> b = std::move(a);
    CHECK_FALSE(static_cast<bool>(a)); // NOLINT(bugprone-use-after-move): moved-from state is specified
    REQUIRE(static_cast<bool>(b));
    CHECK(b() == 42);

    InplaceFunction<int()> c;
    c = std::move(b);
    CHECK(c() == 42);
}

TEST_CASE("InplaceFunction destroys captured state exactly once")
{
    test::LifetimeCounter::reset();
    {
        test::LifetimeCounter counter(7);
        InplaceFunction<int()> a = [counter] {
            return counter.value;
        };
        InplaceFunction<int()> b = std::move(a);
        InplaceFunction<int()> c;
        c = std::move(b);
        CHECK(c() == 7);
        CHECK(test::LifetimeCounter::alive == 2); // the local plus the one stored in c
    }
    CHECK(test::LifetimeCounter::alive == 0);
}
