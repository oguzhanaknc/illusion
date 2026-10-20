#include "TestSupport.hpp"

#include <illusion/core/Memory.hpp>
#include <illusion/text/LocaleId.hpp>
#include <illusion/text/NumberFormat.hpp>
#include <illusion/text/TextOutput.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

using namespace illusion;
using namespace illusion::text;
using namespace std::string_view_literals;

namespace
{

std::string number(f64 value, const NumberFormat& format = {})
{
    std::string out;
    TextOutput output(out);
    formatNumber(value, format, output);
    return out;
}

std::string integer(i64 value, const NumberFormat& format = {})
{
    std::string out;
    TextOutput output(out);
    formatNumber(value, format, output);
    return out;
}

} // namespace

TEST_CASE("locale identifiers parse BCP 47 tags into canonical case")
{
    const auto turkish = LocaleId::parse("tr_tr");
    REQUIRE(turkish.has_value());
    CHECK(turkish->tag() == "tr-TR");
    CHECK(turkish->language() == "tr");
    CHECK(turkish->script().empty());
    CHECK(turkish->region() == "TR");

    const auto chinese = LocaleId::parse("ZH-hant-tw");
    REQUIRE(chinese.has_value());
    CHECK(chinese->tag() == "zh-Hant-TW");
    CHECK(chinese->script() == "Hant");
    CHECK(chinese->region() == "TW");

    CHECK(LocaleId::parse("es-419")->region() == "419");
    CHECK(LocaleId::parse("haw")->tag() == "haw");
    CHECK(LocaleId::parse("en-XA")->tag() == "en-XA");
    CHECK(*LocaleId::parse("pt-br") == *LocaleId::parse("pt-BR"));
    CHECK(LocaleId().empty());
}

TEST_CASE("malformed locale identifiers are rejected")
{
    for (const std::string_view tag :
         {""sv, "e"sv, "engl"sv, "en-"sv, "-en"sv, "en--US"sv, "en-US-x"sv, "en-U"sv, "en-1234"sv,
          "en-Latn-Latn"sv, "en-US-Latn"sv, "e1"sv, "en US"sv, "en-US-1"sv, "en-12"sv, "tr-TR-TR"sv})
    {
        INFO(tag);
        CHECK_FALSE(LocaleId::parse(tag).has_value());
    }
}

TEST_CASE("locale parents drop the region, then the script")
{
    const LocaleId full = *LocaleId::parse("zh-Hant-TW");
    const std::optional<LocaleId> script = full.parent();
    REQUIRE(script.has_value());
    CHECK(script->tag() == "zh-Hant");
    CHECK(script->region().empty());
    const std::optional<LocaleId> language = script->parent();
    REQUIRE(language.has_value());
    CHECK(language->tag() == "zh");
    CHECK_FALSE(language->parent().has_value());
    CHECK(LocaleId::parse("tr-TR")->parent() == LocaleId::parse("tr"));
}

TEST_CASE("numbers use the separators of the locale")
{
    const NumberFormat english;
    const NumberFormat turkish(",", ".");
    const NumberFormat french(",", "\xE2\x80\xAF"); // narrow no-break space
    CHECK(number(1234.5, english) == "1,234.5");
    CHECK(number(1234.5, turkish) == "1.234,5");
    CHECK(number(1234567.25, french) == "1\xE2\x80\xAF"
                                        "234\xE2\x80\xAF"
                                        "567,25");
    CHECK(integer(-1234567, english) == "-1,234,567");
    CHECK(integer(999, turkish) == "999");
    CHECK(integer(1000, turkish) == "1.000");
    CHECK(integer(0) == "0");
    CHECK(integer(std::numeric_limits<i64>::min()) == "-9,223,372,036,854,775,808");
    CHECK(number(12345.0, NumberFormat(".", "")) == "12345");
    CHECK(turkish.decimal() == ",");
    CHECK(turkish.group() == ".");
    CHECK(NumberFormat() == english);
}

TEST_CASE("numbers show at most three fraction digits without trailing zeros")
{
    CHECK(number(1.0) == "1");
    CHECK(number(0.5) == "0.5");
    CHECK(number(0.1234) == "0.123");
    CHECK(number(1234.5678) == "1,234.568");
    CHECK(number(1.0005) == "1");     // 1.0005 is slightly below that in binary
    CHECK(number(2.0005) == "2.001"); // and 2.0005 slightly above
    CHECK(number(0.0625) == "0.062"); // halfway cases round to even
    CHECK(number(-0.0001) == "0");    // no sign on a number that rounds to zero
    CHECK(number(-2.5) == "-2.5");
    CHECK(number(1e21) == "1,000,000,000,000,000,000,000");
    CHECK(number(std::numeric_limits<f64>::infinity()) == "\xE2\x88\x9E");
    CHECK(number(-std::numeric_limits<f64>::infinity()) == "-\xE2\x88\x9E");
    CHECK(number(std::nan("")) == "NaN");
    CHECK(number(std::numeric_limits<f64>::max()).size() > 400); // 309 digits and 102 separators
}

TEST_CASE("text output appends to strings")
{
    std::string out = "prefix:";
    TextOutput output(out);
    output.append("abc");
    output.push('!');
    output.append("");
    CHECK(out == "prefix:abc!");
    CHECK(output.view() == "abc!");
    CHECK(output.size() == 4);
    CHECK_FALSE(output.failed());
}

TEST_CASE("text output builds text in an arena without the heap")
{
    LinearAllocator arena(4096);
    test::ScopedAllocationCounter allocations;
    TextOutput output(arena);
    for (int i = 0; i < 100; ++i)
    {
        output.append("0123456789");
        formatNumber(i64{1234567}, NumberFormat(), output);
    }
    CHECK(output.size() == 100 * 19);
    CHECK(output.view().substr(0, 19) == "01234567891,234,567");
    CHECK_FALSE(output.failed());
    CHECK(allocations.count() == 0);

    LinearAllocator tiny(32);
    TextOutput cut(tiny);
    cut.append("0123456789012345678901234567890123456789");
    CHECK(cut.failed());
    cut.append("more");
    CHECK(cut.failed());
}
