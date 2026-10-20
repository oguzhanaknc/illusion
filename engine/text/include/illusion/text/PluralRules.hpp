#pragma once

/// @file
/// CLDR cardinal plural rules: which plural form ("one", "few", "other", ...) a number takes in a language.
///
/// ```cpp
/// const auto rule = text::pluralRuleForLanguage("ru");
/// text::pluralCategory(*rule, text::PluralOperands::fromInteger(22)) == text::PluralCategory::Few
/// ```
///
/// Rules follow the CLDR cardinal plural rules (CLDR 46) for English, German, Turkish, French, Spanish,
/// Italian, Portuguese (Brazil), Russian, Ukrainian, Polish, Czech, Arabic, Japanese, Chinese and Korean.
/// Compact exponent forms (operand e) are not used, so "many" for French, Spanish, Italian and Portuguese
/// applies to exact multiples of one million.

#include <illusion/core/Base.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion::text
{

/// A plural form.
enum class PluralCategory : u8
{
    Zero,  ///< "zero"
    One,   ///< "one"
    Two,   ///< "two"
    Few,   ///< "few"
    Many,  ///< "many"
    Other, ///< "other": always present
};

/// Number of plural categories.
inline constexpr usize kPluralCategoryCount = 6;

/// The CLDR keyword of @p category, e.g. "few".
[[nodiscard]] std::string_view pluralCategoryName(PluralCategory category) noexcept;

/// Parses a CLDR keyword ("zero", "one", "two", "few", "many" or "other").
[[nodiscard]] std::optional<PluralCategory> pluralCategoryFromName(std::string_view name) noexcept;

/// A set of plural rules shared by one or more languages.
enum class PluralRule : u8
{
    English,    ///< en, de: one (1), other
    Turkish,    ///< tr: one (n = 1, also 1.0), other
    French,     ///< fr: one (0 and 1, including 1.5), many (millions), other
    Spanish,    ///< es: one (n = 1), many (millions), other
    Italian,    ///< it: one (1), many (millions), other
    Portuguese, ///< pt (Brazil): one (0 and 1), many (millions), other
    Russian,    ///< ru, uk: one (1, 21), few (2-4, 22-24), many (0, 5-20), other (fractions)
    Polish,     ///< pl: one (1), few (2-4, 22-24), many (0, 5-21), other (fractions)
    Czech,      ///< cs: one (1), few (2-4), many (fractions), other
    Arabic,     ///< ar: zero, one, two, few (3-10), many (11-99), other
    Japanese,   ///< ja, zh, ko: other only
};

/// The rule of @p language, a lowercase ISO 639 code ("tr") or a language tag ("pt-BR", whose language
/// part is used). Nullopt for languages without built-in rules.
[[nodiscard]] std::optional<PluralRule> pluralRuleForLanguage(std::string_view language) noexcept;

/// The categories @p rule uses, in enum order; always ends with Other.
[[nodiscard]] std::span<const PluralCategory> pluralCategories(PluralRule rule) noexcept;

/// The CLDR operands of a number as it is displayed: i (integer digits), v (number of visible fraction
/// digits) and f (visible fraction digits as an integer). The sign is ignored.
struct PluralOperands
{
    u64 i = 0; ///< Integer digits of the absolute value.
    u32 v = 0; ///< Number of visible fraction digits.
    u64 f = 0; ///< Visible fraction digits, as an integer ("1.50" has v = 2, f = 50).
    /// True for numbers that select Other in every rule (infinities, NaN, integers too large for u64).
    bool otherOnly = false;

    /// The operands of the integer @p value.
    [[nodiscard]] static PluralOperands fromInteger(i64 value) noexcept;

    /// The operands of @p value as formatNumber() shows it: at most three fraction digits, without
    /// trailing zeros (1.0 is shown as "1" and has v = 0). Infinities and NaN select Other.
    [[nodiscard]] static PluralOperands fromNumber(f64 value) noexcept;

    /// The operands of a decimal string such as "1.50" or "-3": digits with an optional '.' and fraction.
    /// Nullopt for anything else.
    [[nodiscard]] static std::optional<PluralOperands> fromDecimal(std::string_view decimal) noexcept;

    /// True if the number has no fraction (n is an integer: v = 0 or f = 0).
    [[nodiscard]] bool isInteger() const noexcept { return f == 0; }
};

/// The plural category of a number with @p operands under @p rule.
[[nodiscard]] PluralCategory pluralCategory(PluralRule rule, const PluralOperands& operands) noexcept;

} // namespace illusion::text
