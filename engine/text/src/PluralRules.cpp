#include "Decimal.hpp"

#include <illusion/text/PluralRules.hpp>

#include <array>

namespace illusion::text
{

namespace
{

constexpr std::array<std::string_view, kPluralCategoryCount> kCategoryNames = {"zero", "one",  "two",
                                                                               "few",  "many", "other"};

using C = PluralCategory;
constexpr C kOneOther[] = {C::One, C::Other};
constexpr C kOneManyOther[] = {C::One, C::Many, C::Other};
constexpr C kOneFewManyOther[] = {C::One, C::Few, C::Many, C::Other};
constexpr C kAll[] = {C::Zero, C::One, C::Two, C::Few, C::Many, C::Other};
constexpr C kOtherOnly[] = {C::Other};

constexpr bool inRange(u64 value, u64 low, u64 high) noexcept
{
    return value >= low && value <= high;
}

// Integers with more than this many digits keep only their last digits (see operandsFromDigits()).
constexpr usize kMaxIntegerDigits = 18;
constexpr u64 kLargeIntegerFlag = 1000000000000000000ull; // 10^18

// i of a digit string. Longer integers become 10^18 plus their last 18 digits: never 0, 1 or 2, and every
// remainder the rules use (modulo 10, 100 and 1000000) is kept.
u64 integerOperand(std::string_view digits) noexcept
{
    const bool large = digits.size() > kMaxIntegerDigits;
    if (large)
    {
        digits.remove_prefix(digits.size() - kMaxIntegerDigits);
    }
    u64 value = 0;
    for (const char c : digits)
    {
        value = value * 10 + static_cast<u64>(c - '0');
    }
    return large ? kLargeIntegerFlag + value : value;
}

PluralOperands operandsFromDigits(std::string_view integer, std::string_view fraction) noexcept
{
    PluralOperands operands;
    operands.i = integerOperand(integer);
    operands.v = static_cast<u32>(fraction.size());
    bool nonZeroTail = false;
    for (usize k = 0; k < fraction.size(); ++k)
    {
        const auto digit = static_cast<u64>(fraction[k] - '0');
        if (k < kMaxIntegerDigits)
        {
            operands.f = operands.f * 10 + digit;
        }
        else
        {
            nonZeroTail = nonZeroTail || digit != 0;
        }
    }
    if (operands.f == 0 && nonZeroTail)
    {
        operands.f = 1; // only whether f is zero matters beyond 18 digits
    }
    return operands;
}

bool isDigits(std::string_view text) noexcept
{
    if (text.empty())
    {
        return false;
    }
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view pluralCategoryName(PluralCategory category) noexcept
{
    const auto index = static_cast<usize>(category);
    return index < kCategoryNames.size() ? kCategoryNames[index] : std::string_view("other");
}

std::optional<PluralCategory> pluralCategoryFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kCategoryNames.size(); ++i)
    {
        if (kCategoryNames[i] == name)
        {
            return static_cast<PluralCategory>(i);
        }
    }
    return std::nullopt;
}

std::optional<PluralRule> pluralRuleForLanguage(std::string_view language) noexcept
{
    language = language.substr(0, language.find_first_of("-_"));
    struct Entry
    {
        std::string_view language;
        PluralRule rule;
    };
    constexpr Entry kLanguages[] = {
        {"en", PluralRule::English},    {"de", PluralRule::English},  {"tr", PluralRule::Turkish},
        {"fr", PluralRule::French},     {"es", PluralRule::Spanish},  {"it", PluralRule::Italian},
        {"pt", PluralRule::Portuguese}, {"ru", PluralRule::Russian},  {"uk", PluralRule::Russian},
        {"pl", PluralRule::Polish},     {"cs", PluralRule::Czech},    {"ar", PluralRule::Arabic},
        {"ja", PluralRule::Japanese},   {"zh", PluralRule::Japanese}, {"ko", PluralRule::Japanese},
    };
    for (const Entry& entry : kLanguages)
    {
        if (entry.language == language)
        {
            return entry.rule;
        }
    }
    return std::nullopt;
}

std::span<const PluralCategory> pluralCategories(PluralRule rule) noexcept
{
    switch (rule)
    {
    case PluralRule::English:
    case PluralRule::Turkish:
        return kOneOther;
    case PluralRule::French:
    case PluralRule::Spanish:
    case PluralRule::Italian:
    case PluralRule::Portuguese:
        return kOneManyOther;
    case PluralRule::Russian:
    case PluralRule::Polish:
    case PluralRule::Czech:
        return kOneFewManyOther;
    case PluralRule::Arabic:
        return kAll;
    case PluralRule::Japanese:
        break;
    }
    return kOtherOnly;
}

PluralOperands PluralOperands::fromInteger(i64 value) noexcept
{
    PluralOperands operands;
    operands.i = value < 0 ? 0 - static_cast<u64>(value) : static_cast<u64>(value);
    return operands;
}

PluralOperands PluralOperands::fromNumber(f64 value) noexcept
{
    char buffer[detail::kDecimalBufferSize];
    const detail::DecimalDigits digits = detail::decimalDigits(value, buffer);
    if (!digits.finite)
    {
        PluralOperands operands;
        operands.otherOnly = true;
        return operands;
    }
    return operandsFromDigits(digits.integer, digits.fraction);
}

std::optional<PluralOperands> PluralOperands::fromDecimal(std::string_view decimal) noexcept
{
    if (!decimal.empty() && decimal.front() == '-')
    {
        decimal.remove_prefix(1);
    }
    const usize point = decimal.find('.');
    const std::string_view integer = decimal.substr(0, point);
    const std::string_view fraction =
        point == std::string_view::npos ? std::string_view() : decimal.substr(point + 1);
    if (!isDigits(integer) || (point != std::string_view::npos && !isDigits(fraction)))
    {
        return std::nullopt;
    }
    return operandsFromDigits(integer, fraction);
}

PluralCategory pluralCategory(PluralRule rule, const PluralOperands& operands) noexcept
{
    if (operands.otherOnly)
    {
        return C::Other;
    }
    const u64 i = operands.i;
    const u32 v = operands.v;
    const bool integer = operands.f == 0; // n is an integer
    const bool million = v == 0 && i != 0 && i % 1000000 == 0;
    switch (rule)
    {
    case PluralRule::English:
        return i == 1 && v == 0 ? C::One : C::Other;
    case PluralRule::Turkish:
        return i == 1 && integer ? C::One : C::Other;
    case PluralRule::French:
        if (i == 0 || i == 1)
        {
            return C::One;
        }
        return million ? C::Many : C::Other;
    case PluralRule::Spanish:
        if (i == 1 && integer)
        {
            return C::One;
        }
        return million ? C::Many : C::Other;
    case PluralRule::Italian:
        if (i == 1 && v == 0)
        {
            return C::One;
        }
        return million ? C::Many : C::Other;
    case PluralRule::Portuguese:
        if (i <= 1)
        {
            return C::One;
        }
        return million ? C::Many : C::Other;
    case PluralRule::Russian:
        if (v != 0)
        {
            return C::Other;
        }
        if (i % 10 == 1 && i % 100 != 11)
        {
            return C::One;
        }
        if (inRange(i % 10, 2, 4) && !inRange(i % 100, 12, 14))
        {
            return C::Few;
        }
        return C::Many; // i % 10 = 0 or 5..9, or i % 100 = 11..14
    case PluralRule::Polish:
        if (v != 0)
        {
            return C::Other;
        }
        if (i == 1)
        {
            return C::One;
        }
        if (inRange(i % 10, 2, 4) && !inRange(i % 100, 12, 14))
        {
            return C::Few;
        }
        return C::Many; // i % 10 = 0..1 (i != 1) or 5..9, or i % 100 = 12..14
    case PluralRule::Czech:
        if (v != 0)
        {
            return C::Many;
        }
        if (i == 1)
        {
            return C::One;
        }
        return inRange(i, 2, 4) ? C::Few : C::Other;
    case PluralRule::Arabic:
        if (!integer)
        {
            return C::Other;
        }
        if (i <= 2)
        {
            return i == 0 ? C::Zero : (i == 1 ? C::One : C::Two);
        }
        if (inRange(i % 100, 3, 10))
        {
            return C::Few;
        }
        return inRange(i % 100, 11, 99) ? C::Many : C::Other;
    case PluralRule::Japanese:
        break;
    }
    return C::Other;
}

} // namespace illusion::text
