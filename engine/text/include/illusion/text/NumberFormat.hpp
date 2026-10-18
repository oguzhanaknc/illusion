#pragma once

/// @file
/// Locale-dependent display of numbers: decimal and grouping separators.
///
/// ```cpp
/// const text::NumberFormat turkish(",", ".");
/// std::string out;
/// text::TextOutput output(out);
/// text::formatNumber(1234.5, turkish, output);   // "1.234,5"
/// ```
///
/// Numbers are shown with groups of three digits and at most three fraction digits (rounded half to
/// even), without trailing zeros: the default decimal format of most locales. Editable fields must not
/// use this; they take the invariant '.' (see the localization manual).

#include <illusion/core/Base.hpp>

#include <string_view>

namespace illusion::text
{

class TextOutput;

/// Separators of a locale. Each is a short UTF-8 string (at most 7 bytes), so that locales whose group
/// separator is a narrow no-break space (U+202F) are supported.
class NumberFormat
{
public:
    /// Longest separator in bytes.
    static constexpr usize kMaxSeparatorBytes = 7;

    /// English separators: "." and ",".
    constexpr NumberFormat() noexcept = default;

    /// Separators @p decimal and @p group; an empty @p group disables grouping. Asserts that each fits in
    /// kMaxSeparatorBytes and that @p decimal is not empty.
    NumberFormat(std::string_view decimal, std::string_view group);

    /// The decimal separator.
    [[nodiscard]] std::string_view decimal() const noexcept { return {m_decimal, m_decimalLength}; }
    /// The grouping separator ("" if numbers are not grouped).
    [[nodiscard]] std::string_view group() const noexcept { return {m_group, m_groupLength}; }

    /// Formats are equal when their separators are.
    [[nodiscard]] friend bool operator==(const NumberFormat& a, const NumberFormat& b) noexcept
    {
        return a.decimal() == b.decimal() && a.group() == b.group();
    }

private:
    char m_decimal[kMaxSeparatorBytes] = {'.'};
    char m_group[kMaxSeparatorBytes] = {','};
    u8 m_decimalLength = 1;
    u8 m_groupLength = 1;
};

/// Appends @p value to @p out with the separators of @p format, e.g. "-1,234,567".
void formatNumber(i64 value, const NumberFormat& format, TextOutput& out);

/// Appends @p value to @p out with the separators of @p format, rounded to at most three fraction digits,
/// e.g. "1,234.5". Infinities are written as "∞" and "-∞", NaN as "NaN".
void formatNumber(f64 value, const NumberFormat& format, TextOutput& out);

} // namespace illusion::text
