#include "TestSupport.hpp"
#include "text/TextTestSupport.hpp"

#include <illusion/core/Memory.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/text/Utf8.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace illusion;
using namespace illusion::text;
using test::compiled;
using test::formatted;
using namespace std::string_view_literals;

namespace
{

const MessageContext kTurkish{PluralRule::Turkish, NumberFormat(",", ".")};
const MessageContext kRussian{PluralRule::Russian, NumberFormat(",", " ")};

constexpr std::string_view kFiles = "{count, plural, =0 {No files} one {# file} other {# files}}";

} // namespace

TEST_CASE("messages substitute string and number arguments")
{
    CHECK(formatted("Hello, {name}!", {{"name", "Ada"}}) == "Hello, Ada!");
    CHECK(formatted("{a}{b}{a}", {{"a", "x"}, {"b", 2}}) == "x2x");
    CHECK(formatted("Size: {bytes, number} bytes", {{"bytes", 1234567}}) == "Size: 1,234,567 bytes");
    CHECK(formatted("{ratio}", {{"ratio", 0.125}}) == "0.125");
    CHECK(formatted("{n, number}", {{"n", 1234.5}}, kTurkish) == "1.234,5");
    CHECK(formatted("{  spaced  }", {{"spaced", "ok"}}) == "ok");
    CHECK(formatted("no arguments", {}) == "no arguments");
    CHECK(formatted("", {}) == "");
    const std::string owned = "owned";
    CHECK(formatted("{s}", {{"s", owned}}) == "owned");
    CHECK(formatted("{big}", {{"big", 18446744073709551615ull}}) == "18,446,744,073,709,551,616");
}

TEST_CASE("plural messages pick exact values, then the category of the language")
{
    const MessageFormat files = compiled(kFiles);
    CHECK(formatted(files, {{"count", 0}}) == "No files");
    CHECK(formatted(files, {{"count", 1}}) == "1 file");
    CHECK(formatted(files, {{"count", 2}}) == "2 files");
    CHECK(formatted(files, {{"count", 1234}}) == "1,234 files");
    CHECK(formatted(files, {{"count", 1.5}}) == "1.5 files");
    CHECK(formatted(files, {{"count", 0.0}}) == "No files"); // =0 also matches a real zero

    const MessageFormat russian =
        compiled("{n, plural, one {# файл} few {# файла} many {# файлов} other {# файла}}");
    CHECK(formatted(russian, {{"n", 1}}, kRussian) == "1 файл");
    CHECK(formatted(russian, {{"n", 22}}, kRussian) == "22 файла");
    CHECK(formatted(russian, {{"n", 5}}, kRussian) == "5 файлов");
    CHECK(formatted(russian, {{"n", 1.5}}, kRussian) == "1,5 файла");
    CHECK(formatted(russian, {{"n", 12345}}, kRussian) == "12 345 файлов");

    // A category the rule never selects is allowed; a missing one falls back to "other".
    CHECK(formatted("{n, plural, few {few} other {other}}", {{"n", 3}}) == "other");
    CHECK(formatted("{n, plural, =-1 {minus one} other {#}}", {{"n", -1}}) == "minus one");
}

TEST_CASE("select messages pick by string value and nest with plurals")
{
    const MessageFormat joined =
        compiled("{gender, select, female {{name} joined her team} male {{name} joined his "
                 "team} other {{name} joined their team}}");
    CHECK(formatted(joined, {{"gender", "female"}, {"name", "Ada"}}) == "Ada joined her team");
    CHECK(formatted(joined, {{"gender", "male"}, {"name", "Bo"}}) == "Bo joined his team");
    CHECK(formatted(joined, {{"gender", "robot"}, {"name", "R2"}}) == "R2 joined their team");

    // '#' inside a select inside a plural is the plural's number.
    const MessageFormat nested = compiled("{n, plural, one {{kind, select, file {# file} other {# item}}} "
                                          "other {{kind, select, file {# files} other "
                                          "{# items}}}}");
    CHECK(formatted(nested, {{"n", 1}, {"kind", "file"}}) == "1 file");
    CHECK(formatted(nested, {{"n", 3}, {"kind", "folder"}}) == "3 items");

    const MessageFormat inner = compiled("{a, plural, other {{b, plural, one {# b} other {# bs}} and # a}}");
    CHECK(formatted(inner, {{"a", 5}, {"b", 1}}) == "1 b and 5 a");
}

TEST_CASE("apostrophes quote syntax characters")
{
    CHECK(formatted("It''s '{'literal'}'", {}) == "It's {literal}");
    CHECK(formatted("don't", {}) == "don't"); // a lone apostrophe is literal
    CHECK(formatted("'{name}'", {{"name", "x"}}) == "{name}");
    CHECK(formatted("'{''}'", {}) == "{'}");    // '' inside a quote
    CHECK(formatted("# hash", {}) == "# hash"); // '#' is literal outside plurals
    CHECK(formatted("{n, plural, other {'#' is #}}", {{"n", 7}}) == "# is 7");
    CHECK(formatted("{n, plural, other {it''s #}}", {{"n", 2}}) == "it's 2");
}

TEST_CASE("malformed patterns return errors with offsets and never assert")
{
    struct Case
    {
        std::string_view pattern;
        u32 offset;
    };
    const Case cases[] = {
        {"{", 1},
        {"}", 0},
        {"text {name", 5},
        {"{1}", 1},
        {"{name, unknown}", 7},
        {"{name, number, integer}", 13},
        {"{n, plural}", 10},
        {"{n, plural, one {x}}", 0},
        {"{n, plural, two {x} other {y}", 0},
        {"{n, plural, several {x} other {y}}", 12},
        {"{n, plural, offset:1 other {#}}", 12},
        {"{n, plural, one {x} one {y} other {z}}", 20},
        {"{n, plural, =1 {a} =1 {b} other {c}}", 19},
        {"{n, plural, =x {a} other {b}}", 12},
        {"{n, plural, one x other {y}}", 16},
        {"{s, select, a {x}}", 0},
        {"{s, select, other {x} other {y}}", 22},
        {"'{unterminated", 0},
        {"bad \xFF utf8", 4},
        {"{a, plural, other {{b, plural, other {{c, plural, other {{d, plural, other {{e, plural, other {{f, "
         "plural, "
         "other {{g, plural, other {{h, plural, other {{i, plural, other {x}}}}}}}}}}}}}}}}}}",
         152}, // the ninth placeholder
    };
    for (const Case& c : cases)
    {
        INFO(c.pattern);
        const auto message = MessageFormat::compile(c.pattern);
        REQUIRE_FALSE(message.hasValue());
        CHECK(message.error().offset == c.offset);
        CHECK_FALSE(message.error().message.empty());
    }
    // Eight levels are fine.
    CHECK(MessageFormat::compile(
              "{a, plural, other {{b, plural, other {{c, plural, other {{d, plural, other {{e, "
              "plural, other {{f, plural, other {{g, plural, other {{h, plural, other "
              "{x}}}}}}}}}}}}}}}}")
              .hasValue());
}

TEST_CASE("random input never asserts in the compiler")
{
    test::ScopedAssertCatcher catcher; // an assert would throw and fail the test
    Random random(29);
    constexpr std::string_view kAlphabet = "{}#',= abcnpluralselectother0123456789-\xC3\xA7";
    for (int round = 0; round < 3000; ++round)
    {
        std::string pattern;
        const u64 length = random.below(24);
        for (u64 i = 0; i < length; ++i)
        {
            pattern += kAlphabet[random.below(kAlphabet.size())];
        }
        const auto message = MessageFormat::compile(pattern);
        if (message)
        {
            (void)formatted(*message, {{"a", 1}, {"n", 2}});
            CHECK(MessageFormat::compile(message->toPattern()).hasValue());
        }
    }
}

TEST_CASE("formatting problems are reported but the message is still written")
{
    std::string out;
    TextOutput output(out);
    const auto missing =
        compiled("Hi {name}, {n, plural, one {# file} other {# files}}").format({}, {}, output);
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().message.find("name") != std::string::npos);
    CHECK(out == "Hi {name}, {n} files");

    const MessageFormat number = compiled("{n, number} {n, plural, one {one} other {other}}");
    std::string text;
    TextOutput textOutput(text);
    const auto mismatch = number.format({{"n", "seven"}}, {}, textOutput);
    REQUIRE_FALSE(mismatch.hasValue());
    CHECK(mismatch.error().message == "argument 'n' is not a number");
    CHECK(text == "seven other");

    CHECK(formatted("{s, select, other {x}}", {{"s", 3}}) == "x");
}

TEST_CASE("compiled messages list their arguments and plural forms")
{
    const MessageFormat message = compiled(
        "{z} {a, plural, one {{m}} other {{b, plural, zero {} few {} other {}}}} {s, select, other {}}");
    CHECK(std::vector<std::string>(message.argumentNames().begin(), message.argumentNames().end()) ==
          std::vector<std::string>{"a", "b", "m", "s", "z"});
    REQUIRE(message.pluralCategoryMasks().size() == 2);
    CHECK(message.pluralCategoryMasks()[0] == ((1u << 1) | (1u << 5))); // one, other
    CHECK(message.pluralCategoryMasks()[1] == ((1u << 0) | (1u << 3) | (1u << 5)));
    CHECK(compiled("{n, plural, =0 {} other {}}").pluralCategoryMasks()[0] == (1u << 5));
}

TEST_CASE("messages write back as canonical patterns")
{
    CHECK(compiled("{ n ,plural,=0{none}one{# x}other{# xs}}").toPattern() ==
          "{n, plural, =0 {none} one {# x} other {# xs}}");
    CHECK(compiled("It''s '{'q'}' {v , number}").toPattern() == "It''s '{'q'}' {v, number}");
    CHECK(compiled("{n, plural, other {'#' #}}").toPattern() == "{n, plural, other {'#' #}}");
    CHECK(MessageFormat().toPattern().empty());
}

TEST_CASE("pseudo-localization accents text, pads it and keeps the structure")
{
    const MessageFormat files = compiled(kFiles);
    const MessageFormat pseudo = files.pseudoLocalized();
    CHECK(std::vector<std::string>(pseudo.argumentNames().begin(), pseudo.argumentNames().end()) ==
          std::vector<std::string>{"count"});
    const std::string one = formatted(pseudo, {{"count", 1}});
    CHECK(one.front() == '[');
    CHECK(one.back() == ']');
    CHECK(one.find("1 ") != std::string::npos);                        // the number is intact
    CHECK(one.find("f\xC3\xAD\xC4\xBE\xC3\xA9") != std::string::npos); // "file" accented

    const std::string english = "Open the project settings";
    const std::string padded = formatted(compiled(english).pseudoLocalized(), {});
    const usize expected = english.size() + (english.size() * 35 + 50) / 100 + 2; // padding and brackets
    CHECK(utf8Length(padded) == expected);
    CHECK(formatted(compiled("{name}").pseudoLocalized(), {{"name", "Ada"}}) == "[Ada]");
}

TEST_CASE("formatting into an arena allocates nothing")
{
    const MessageFormat files = compiled(kFiles);
    const MessageFormat joined =
        compiled("{gender, select, female {{name} joined her team} other {{name} joined}}");
    LinearAllocator arena(16 * 1024);
    test::ScopedAllocationCounter allocations;
    for (int i = 0; i < 100; ++i)
    {
        TextOutput out(arena);
        REQUIRE(files.format({{"count", i}}, {}, out).hasValue());
        REQUIRE(joined.format({{"gender", "female"}, {"name", "Ada"}}, kTurkish, out).hasValue());
        arena.reset();
    }
    CHECK(allocations.count() == 0);
}
