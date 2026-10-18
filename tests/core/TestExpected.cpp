#include "TestSupport.hpp"

#include <illusion/core/Expected.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using namespace illusion;
using test::LifetimeCounter;

namespace
{

Expected<int, std::string> parseDigit(char c)
{
    if (c < '0' || c > '9')
    {
        return unexpected(std::string("not a digit"));
    }
    return c - '0';
}

Expected<void, int> succeedIf(bool ok)
{
    if (!ok)
    {
        return unexpected(7);
    }
    return {};
}

constexpr int constexprValue()
{
    Expected<int, int> a = 5;
    Expected<int, int> b = unexpected(3);
    b = a;
    return *b + a.valueOr(0);
}

} // namespace

static_assert(constexprValue() == 10, "Expected works in constant expressions");
static_assert(std::is_nothrow_move_constructible_v<Expected<std::string, std::string>>);
static_assert(std::is_nothrow_move_assignable_v<Expected<std::string, std::string>>);
static_assert(std::is_nothrow_move_constructible_v<Expected<std::unique_ptr<int>, int>>);
static_assert(!std::is_copy_constructible_v<Expected<std::unique_ptr<int>, int>>);
static_assert(std::is_nothrow_move_constructible_v<Expected<void, std::string>>);
static_assert(!std::is_convertible_v<Expected<int, int>, bool>, "operator bool is explicit");

TEST_CASE("Expected holds a value or an error")
{
    const auto digit = parseDigit('7');
    REQUIRE(digit.hasValue());
    CHECK(static_cast<bool>(digit));
    CHECK(digit.value() == 7);
    CHECK(*digit == 7);

    const auto letter = parseDigit('x');
    REQUIRE_FALSE(letter.hasValue());
    CHECK_FALSE(static_cast<bool>(letter));
    CHECK(letter.error() == "not a digit");
}

TEST_CASE("Expected default-constructs a value-initialized value")
{
    const Expected<int, std::string> value;
    REQUIRE(value.hasValue());
    CHECK(*value == 0);
}

TEST_CASE("Expected gives access to members of the value")
{
    Expected<std::string, int> text = std::string("hello");
    CHECK(text->size() == 5);
    text->append("!");
    CHECK(*text == "hello!");
    const Expected<std::string, int>& constText = text;
    CHECK(constText->front() == 'h');
}

TEST_CASE("valueOr returns the fallback only for errors")
{
    CHECK(parseDigit('3').valueOr(-1) == 3);
    CHECK(parseDigit('?').valueOr(-1) == -1);
    Expected<std::unique_ptr<int>, int> owned = std::make_unique<int>(4);
    const std::unique_ptr<int> taken = std::move(owned).valueOr(nullptr);
    REQUIRE(taken != nullptr);
    CHECK(*taken == 4);
}

TEST_CASE("Expected moves values and errors out")
{
    Expected<std::unique_ptr<int>, std::string> value = std::make_unique<int>(9);
    std::unique_ptr<int> pointer = std::move(value).value();
    REQUIRE(pointer != nullptr);
    CHECK(*pointer == 9);

    Expected<int, std::string> error = unexpected(std::string(100, 'e'));
    const std::string message = std::move(error).error();
    CHECK(message.size() == 100);
}

TEST_CASE("Expected copies and assigns across alternatives")
{
    Expected<std::string, std::string> a = std::string("value");
    Expected<std::string, std::string> b = unexpected(std::string("error"));

    Expected<std::string, std::string> c = a;
    CHECK(c.hasValue());
    CHECK(*c == "value");

    c = b; // value -> error
    REQUIRE_FALSE(c.hasValue());
    CHECK(c.error() == "error");

    c = a; // error -> value
    REQUIRE(c.hasValue());
    CHECK(*c == "value");

    c = std::move(b); // move value -> error
    REQUIRE_FALSE(c.hasValue());
    CHECK(c.error() == "error");

    Expected<std::string, std::string>& self = c;
    c = self;
    CHECK(c.error() == "error");
}

TEST_CASE("Expected constructs and destroys its contents exactly once")
{
    LifetimeCounter::reset();
    {
        Expected<LifetimeCounter, LifetimeCounter> value = LifetimeCounter(1);
        Expected<LifetimeCounter, LifetimeCounter> error = unexpected(LifetimeCounter(2));
        CHECK(LifetimeCounter::alive == 2);

        Expected<LifetimeCounter, LifetimeCounter> copy = value;
        CHECK(LifetimeCounter::alive == 3);
        copy = error; // destroys the value, copies the error
        CHECK(LifetimeCounter::alive == 3);
        CHECK(copy.error().value == 2);

        Expected<LifetimeCounter, LifetimeCounter> moved = std::move(value);
        CHECK(LifetimeCounter::alive == 4); // moved-from objects stay alive until destroyed
        CHECK(moved->value == 1);
        CHECK(value->value == -1);

        moved = std::move(copy);
        CHECK(LifetimeCounter::alive == 4);
        CHECK(moved.error().value == 2);
    }
    CHECK(LifetimeCounter::alive == 0);
}

TEST_CASE("Expected<void> reports success or an error")
{
    const auto ok = succeedIf(true);
    CHECK(ok.hasValue());
    ok.value();

    auto failed = succeedIf(false);
    REQUIRE_FALSE(failed.hasValue());
    CHECK(failed.error() == 7);

    Expected<void, std::string> a;
    Expected<void, std::string> b = unexpected(std::string("broken"));
    a = b;
    CHECK(a.error() == "broken");
    b = Expected<void, std::string>();
    CHECK(b.hasValue());
    a = std::move(b);
    CHECK(a.hasValue());
}

TEST_CASE("Expected<void> destroys its error exactly once")
{
    LifetimeCounter::reset();
    {
        Expected<void, LifetimeCounter> error = unexpected(LifetimeCounter(3));
        Expected<void, LifetimeCounter> copy = error;
        CHECK(LifetimeCounter::alive == 2);
        copy = Expected<void, LifetimeCounter>();
        CHECK(LifetimeCounter::alive == 1);
        copy = std::move(error);
        CHECK(LifetimeCounter::alive == 2);
    }
    CHECK(LifetimeCounter::alive == 0);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("Accessing the wrong alternative of an Expected asserts")
{
    test::ScopedAssertCatcher catcher;
    const auto value = parseDigit('1');
    const auto error = parseDigit('a');
    CHECK_THROWS_AS((void)value.error(), test::AssertionFailure);
    CHECK_THROWS_AS((void)error.value(), test::AssertionFailure);
    CHECK_THROWS_AS((void)*error, test::AssertionFailure);
    const Expected<void, int> success;
    CHECK_THROWS_AS((void)success.error(), test::AssertionFailure);
    const Expected<void, int> failure = unexpected(1);
    CHECK_THROWS_AS(failure.value(), test::AssertionFailure);
}
#endif
