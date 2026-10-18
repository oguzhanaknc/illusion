#include "Decimal.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/text/NumberFormat.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/text/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace illusion::text
{

namespace detail
{

DecimalDigits decimalDigits(f64 value, char (&buffer)[kDecimalBufferSize]) noexcept
{
    DecimalDigits digits;
    if (std::isnan(value))
    {
        digits.finite = false;
        digits.nan = true;
        return digits;
    }
    digits.negative = std::signbit(value);
    if (std::isinf(value))
    {
        digits.finite = false;
        return digits;
    }
    const auto [end, error] =
        std::to_chars(buffer, buffer + kDecimalBufferSize, std::fabs(value), std::chars_format::fixed, 3);
    ILL_ASSERT(error == std::errc(), "a finite double did not fit the decimal buffer");
    std::string_view text(buffer, static_cast<usize>(end - buffer));
    const usize point = text.find('.');
    digits.integer = text.substr(0, point);
    std::string_view fraction = point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
    while (!fraction.empty() && fraction.back() == '0')
    {
        fraction.remove_suffix(1);
    }
    digits.fraction = fraction;
    // -0.0001 rounds to 0, which is shown without a sign.
    if (digits.integer == "0" && digits.fraction.empty())
    {
        digits.negative = false;
    }
    return digits;
}

DecimalDigits decimalDigits(i64 value, char (&buffer)[kDecimalBufferSize]) noexcept
{
    DecimalDigits digits;
    digits.negative = value < 0;
    // The magnitude of INT64_MIN does not fit in i64.
    const u64 magnitude = value < 0 ? 0 - static_cast<u64>(value) : static_cast<u64>(value);
    const auto [end, error] = std::to_chars(buffer, buffer + kDecimalBufferSize, magnitude);
    ILL_ASSERT(error == std::errc(), "an integer did not fit the decimal buffer");
    digits.integer = std::string_view(buffer, static_cast<usize>(end - buffer));
    return digits;
}

} // namespace detail

namespace
{

void writeDigits(const detail::DecimalDigits& digits, const NumberFormat& format, TextOutput& out)
{
    if (!digits.finite)
    {
        if (digits.nan)
        {
            out.append("NaN");
            return;
        }
        out.append(digits.negative ? "-\xE2\x88\x9E" : "\xE2\x88\x9E");
        return;
    }
    if (digits.negative)
    {
        out.push('-');
    }
    const std::string_view integer = digits.integer;
    const std::string_view group = format.group();
    // The first group takes the digits that do not make up a full group of three.
    usize first = integer.size() % 3;
    if (first == 0)
    {
        first = 3;
    }
    out.append(integer.substr(0, first));
    for (usize i = first; i < integer.size(); i += 3)
    {
        out.append(group);
        out.append(integer.substr(i, 3));
    }
    if (!digits.fraction.empty())
    {
        out.append(format.decimal());
        out.append(digits.fraction);
    }
}

void copySeparator(std::string_view separator, char (&storage)[NumberFormat::kMaxSeparatorBytes], u8& length)
{
    ILL_ASSERT(separator.size() <= NumberFormat::kMaxSeparatorBytes,
               "number separator '{}' is longer than {} bytes", separator, NumberFormat::kMaxSeparatorBytes);
    ILL_ASSERT(isValidUtf8(separator), "number separator is not valid UTF-8");
    const usize size = separator.size() <= NumberFormat::kMaxSeparatorBytes ? separator.size() : 0;
    std::memset(storage, 0, sizeof(storage));
    std::copy_n(separator.data(), size, storage); // copy_n accepts the null data of an empty view
    length = static_cast<u8>(size);
}

} // namespace

NumberFormat::NumberFormat(std::string_view decimal, std::string_view group)
{
    ILL_ASSERT(!decimal.empty(), "the decimal separator must not be empty");
    copySeparator(decimal, m_decimal, m_decimalLength);
    copySeparator(group, m_group, m_groupLength);
}

void formatNumber(i64 value, const NumberFormat& format, TextOutput& out)
{
    char buffer[detail::kDecimalBufferSize];
    writeDigits(detail::decimalDigits(value, buffer), format, out);
}

void formatNumber(f64 value, const NumberFormat& format, TextOutput& out)
{
    char buffer[detail::kDecimalBufferSize];
    writeDigits(detail::decimalDigits(value, buffer), format, out);
}

} // namespace illusion::text
