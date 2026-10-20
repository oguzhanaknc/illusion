#pragma once

// Decimal digits of a number as it is displayed, shared by number formatting and plural selection so that
// "1.0" shown as "1" also selects the plural category of 1.

#include <illusion/core/Base.hpp>

#include <string_view>

namespace illusion::text::detail
{

struct DecimalDigits
{
    bool negative = false;
    bool finite = true;        // false for infinities and NaN
    bool nan = false;          // NaN (finite is false)
    std::string_view integer;  // integer digits without sign, at least "0"
    std::string_view fraction; // up to three fraction digits, no trailing zeros
};

// Size of the buffer decimalDigits() needs: the integer digits of the largest double and ".000".
inline constexpr usize kDecimalBufferSize = 320;

// Digits of @p value rounded to three fraction digits (half to even), written into @p buffer.
DecimalDigits decimalDigits(f64 value, char (&buffer)[kDecimalBufferSize]) noexcept;

// Digits of @p value, written into @p buffer.
DecimalDigits decimalDigits(i64 value, char (&buffer)[kDecimalBufferSize]) noexcept;

} // namespace illusion::text::detail
