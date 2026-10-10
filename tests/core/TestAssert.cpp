#include "TestSupport.hpp"

#include <illusion/core/Assert.hpp>

#include <doctest/doctest.h>

using namespace illusion;

namespace
{

int g_evaluations = 0;

bool countedTrue()
{
    ++g_evaluations;
    return true;
}

void failingCheck(int value)
{
    ILL_CHECK(value > 10, "value was {}", value);
}

} // namespace

TEST_CASE("ILL_CHECK calls the installed handler with expression, message and location")
{
    test::ScopedAssertCatcher catcher;
    CHECK_NOTHROW(failingCheck(11));
    try
    {
        failingCheck(3);
        FAIL("ILL_CHECK did not fire");
    }
    catch (const test::AssertionFailure& failure)
    {
        const std::string what = failure.what();
        CHECK(what.find("value > 10") != std::string::npos);
        CHECK(what.find("value was 3") != std::string::npos);
    }
}

TEST_CASE("setAssertHandler returns the previous handler and nullptr restores the default")
{
    const AssertHandler original = assertHandler();
    REQUIRE(original != nullptr);
    {
        test::ScopedAssertCatcher catcher;
        CHECK(assertHandler() != original);
    }
    CHECK(assertHandler() == original);

    const AssertHandler previous = setAssertHandler(nullptr);
    CHECK(previous == original);
    CHECK(assertHandler() == original);
}

TEST_CASE("ILL_VERIFY always evaluates its condition")
{
    g_evaluations = 0;
    ILL_VERIFY(countedTrue());
    CHECK(g_evaluations == 1);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("ILL_ASSERT fires when enabled")
{
    test::ScopedAssertCatcher catcher;
    const auto assertPositive = [](int v) {
        ILL_ASSERT(v > 0);
    };
    CHECK_NOTHROW(assertPositive(1));
    CHECK_THROWS_AS(assertPositive(-1), test::AssertionFailure);
}
#else
TEST_CASE("ILL_ASSERT does not evaluate its condition when disabled")
{
    g_evaluations = 0;
    ILL_ASSERT(countedTrue());
    CHECK(g_evaluations == 0);
}
#endif
