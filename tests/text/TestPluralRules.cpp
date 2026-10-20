#include <illusion/text/PluralRules.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

using namespace illusion;
using namespace illusion::text;
using namespace std::string_view_literals;

namespace
{

using C = PluralCategory;

PluralCategory categoryOf(PluralRule rule, std::string_view decimal)
{
    const std::optional<PluralOperands> operands = PluralOperands::fromDecimal(decimal);
    REQUIRE_MESSAGE(operands.has_value(), decimal);
    return pluralCategory(rule, *operands);
}

// Checks the CLDR sample numbers of one category ("1.0" style samples keep their visible fraction digits).
void checkSamples(PluralRule rule, PluralCategory expected, std::initializer_list<std::string_view> samples)
{
    for (const std::string_view sample : samples)
    {
        INFO("rule ", static_cast<int>(rule), " sample ", sample);
        CHECK(categoryOf(rule, sample) == expected);
    }
}

} // namespace

TEST_CASE("plural category names round-trip")
{
    for (usize i = 0; i < kPluralCategoryCount; ++i)
    {
        const auto category = static_cast<PluralCategory>(i);
        CHECK(pluralCategoryFromName(pluralCategoryName(category)) == category);
    }
    CHECK(pluralCategoryName(C::Few) == "few");
    CHECK_FALSE(pluralCategoryFromName("several").has_value());
}

TEST_CASE("languages map to their plural rules")
{
    CHECK(pluralRuleForLanguage("en") == PluralRule::English);
    CHECK(pluralRuleForLanguage("de") == PluralRule::English);
    CHECK(pluralRuleForLanguage("tr") == PluralRule::Turkish);
    CHECK(pluralRuleForLanguage("pt-BR") == PluralRule::Portuguese);
    CHECK(pluralRuleForLanguage("uk") == PluralRule::Russian);
    CHECK(pluralRuleForLanguage("zh_Hant") == PluralRule::Japanese);
    CHECK(pluralRuleForLanguage("ko") == PluralRule::Japanese);
    CHECK_FALSE(pluralRuleForLanguage("xx").has_value());
    CHECK(pluralCategories(PluralRule::Arabic).size() == 6);
    CHECK(pluralCategories(PluralRule::Japanese).size() == 1);
    CHECK(pluralCategories(PluralRule::Russian).back() == C::Other);
}

TEST_CASE("English, German and Turkish plural rules match the CLDR samples")
{
    checkSamples(PluralRule::English, C::One, {"1"});
    checkSamples(PluralRule::English, C::Other, {"0", "2", "16", "100", "1000", "0.0", "1.0", "1.5", "10.0"});
    checkSamples(PluralRule::Turkish, C::One, {"1", "1.0", "1.00", "1.000"});
    checkSamples(PluralRule::Turkish, C::Other, {"0", "2", "16", "100", "0.0", "0.9", "1.1", "1.6", "10.0"});
}

TEST_CASE("French, Spanish, Italian and Portuguese plural rules match the CLDR samples")
{
    checkSamples(PluralRule::French, C::One, {"0", "1", "0.0", "1.5"});
    checkSamples(PluralRule::French, C::Many, {"1000000", "2000000", "10000000"});
    checkSamples(PluralRule::French, C::Other,
                 {"2", "17", "100", "1000", "10000", "100000", "2.0", "3.5", "1000000.5"});
    checkSamples(PluralRule::Spanish, C::One, {"1", "1.0", "1.00"});
    checkSamples(PluralRule::Spanish, C::Many, {"1000000", "3000000"});
    checkSamples(PluralRule::Spanish, C::Other, {"0", "2", "16", "100", "1000", "0.0", "0.9", "1.1", "1.6"});
    checkSamples(PluralRule::Italian, C::One, {"1"});
    checkSamples(PluralRule::Italian, C::Many, {"1000000"});
    checkSamples(PluralRule::Italian, C::Other,
                 {"0", "2", "16", "100", "1000", "100000", "0.0", "1.0", "1.5"});
    checkSamples(PluralRule::Portuguese, C::One, {"0", "1", "0.0", "1.5"});
    checkSamples(PluralRule::Portuguese, C::Many, {"1000000"});
    checkSamples(PluralRule::Portuguese, C::Other, {"2", "17", "100", "1000", "2.0", "3.5"});
}

TEST_CASE("Russian, Ukrainian, Polish and Czech plural rules match the CLDR samples")
{
    checkSamples(PluralRule::Russian, C::One, {"1", "21", "31", "41", "51", "61", "71", "81", "101", "1001"});
    checkSamples(PluralRule::Russian, C::Few,
                 {"2", "3", "4", "22", "23", "24", "32", "33", "34", "102", "1002"});
    checkSamples(PluralRule::Russian, C::Many,
                 {"0", "5", "11", "12", "14", "19", "100", "111", "1000", "10000"});
    checkSamples(PluralRule::Russian, C::Other, {"0.0", "1.5", "2.0", "10.0", "100.0"});
    checkSamples(PluralRule::Polish, C::One, {"1"});
    checkSamples(PluralRule::Polish, C::Few, {"2", "3", "4", "22", "23", "24", "32", "102", "1002"});
    checkSamples(PluralRule::Polish, C::Many, {"0", "5", "11", "12", "14", "19", "21", "100", "101", "1000"});
    checkSamples(PluralRule::Polish, C::Other, {"0.0", "1.5", "10.0", "100.0"});
    checkSamples(PluralRule::Czech, C::One, {"1"});
    checkSamples(PluralRule::Czech, C::Few, {"2", "3", "4"});
    checkSamples(PluralRule::Czech, C::Many, {"0.0", "1.5", "10.0", "100.0"});
    checkSamples(PluralRule::Czech, C::Other, {"0", "5", "19", "100", "1000", "10000"});
}

TEST_CASE("Arabic and Japanese plural rules match the CLDR samples")
{
    checkSamples(PluralRule::Arabic, C::Zero, {"0", "0.0", "0.00"});
    checkSamples(PluralRule::Arabic, C::One, {"1", "1.0"});
    checkSamples(PluralRule::Arabic, C::Two, {"2", "2.0"});
    checkSamples(PluralRule::Arabic, C::Few, {"3", "10", "103", "110", "1003", "3.0"});
    checkSamples(PluralRule::Arabic, C::Many, {"11", "26", "99", "111", "1011", "11.0"});
    checkSamples(PluralRule::Arabic, C::Other, {"100", "102", "200", "202", "1000", "0.1", "0.9", "1.1"});
    checkSamples(PluralRule::Japanese, C::Other, {"0", "1", "2", "1000000", "1.5"});
}

TEST_CASE("plural operands come from integers, displayed numbers and decimal strings")
{
    const PluralOperands negative = PluralOperands::fromInteger(-21);
    CHECK(negative.i == 21);
    CHECK(negative.v == 0);
    CHECK(pluralCategory(PluralRule::Russian, negative) == C::One);
    CHECK(PluralOperands::fromInteger(std::numeric_limits<i64>::min()).i == 9223372036854775808ull);

    // Numbers are shown with at most three fraction digits, so 1.0 is "1" and selects "one".
    CHECK(pluralCategory(PluralRule::English, PluralOperands::fromNumber(1.0)) == C::One);
    CHECK(pluralCategory(PluralRule::English, PluralOperands::fromNumber(1.5)) == C::Other);
    CHECK(pluralCategory(PluralRule::English, PluralOperands::fromNumber(1.0001)) == C::One);
    const PluralOperands half = PluralOperands::fromNumber(2.5);
    CHECK(half.i == 2);
    CHECK(half.v == 1);
    CHECK(half.f == 5);
    CHECK(pluralCategory(PluralRule::Arabic, PluralOperands::fromNumber(std::nan(""))) == C::Other);
    CHECK(pluralCategory(PluralRule::English, PluralOperands::fromNumber(-1.0)) == C::One);

    const auto decimal = PluralOperands::fromDecimal("-1.50");
    REQUIRE(decimal.has_value());
    CHECK(decimal->i == 1);
    CHECK(decimal->v == 2);
    CHECK(decimal->f == 50);
    for (const std::string_view bad : {""sv, "-"sv, "1."sv, ".5"sv, "1e3"sv, "1,5"sv, "abc"sv, "1.2.3"sv})
    {
        INFO(bad);
        CHECK_FALSE(PluralOperands::fromDecimal(bad).has_value());
    }
}

TEST_CASE("very large integers keep the remainders plural rules use")
{
    // 10^30 is a multiple of a million, ends in 0 and is neither 1 nor 2.
    CHECK(categoryOf(PluralRule::French, "1000000000000000000000000000000") == C::Many);
    CHECK(categoryOf(PluralRule::Russian, "1000000000000000000000000000021") == C::One);
    CHECK(categoryOf(PluralRule::Arabic, "1000000000000000000000000000001") == C::Other);
    CHECK(categoryOf(PluralRule::English, "1000000000000000000000000000001") == C::Other);
    CHECK(pluralCategory(PluralRule::French, PluralOperands::fromNumber(1e21)) == C::Many); // exact in binary
}
