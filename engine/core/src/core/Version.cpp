#include <illusion/core/Version.hpp>

#include <charconv>
#include <format>
#include <limits>

namespace illusion
{
namespace
{

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool isNumeric(std::string_view identifier) noexcept
{
    for (const char c : identifier)
    {
        if (!isDigit(c))
        {
            return false;
        }
    }
    return true;
}

// A numeric part without leading zeros that fits in u32.
std::optional<u32> parseNumber(std::string_view text) noexcept
{
    if (text.empty() || !isNumeric(text) || (text.size() > 1 && text[0] == '0'))
    {
        return std::nullopt;
    }
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

// Dot-separated non-empty identifiers of [0-9A-Za-z-]; numeric pre-release identifiers have no leading
// zeros.
bool isValidIdentifierList(std::string_view text, bool numericLeadingZerosAllowed) noexcept
{
    if (text.empty())
    {
        return false;
    }
    usize start = 0;
    while (start <= text.size())
    {
        const usize end = std::min(text.find('.', start), text.size());
        const std::string_view identifier = text.substr(start, end - start);
        if (identifier.empty())
        {
            return false;
        }
        for (const char c : identifier)
        {
            const bool valid = isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
            if (!valid)
            {
                return false;
            }
        }
        if (!numericLeadingZerosAllowed && identifier.size() > 1 && identifier[0] == '0' &&
            isNumeric(identifier))
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

// Compares one pre-release identifier: numeric ones numerically and below alphanumeric ones.
std::strong_ordering compareIdentifier(std::string_view a, std::string_view b) noexcept
{
    const bool numericA = isNumeric(a);
    const bool numericB = isNumeric(b);
    if (numericA && numericB)
    {
        // No leading zeros, so a longer number is larger.
        if (a.size() != b.size())
        {
            return a.size() <=> b.size();
        }
        return a.compare(b) <=> 0;
    }
    if (numericA != numericB)
    {
        return numericA ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return a.compare(b) <=> 0;
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::parse(std::string_view text)
{
    if (const usize plus = text.find('+'); plus != std::string_view::npos)
    {
        if (!isValidIdentifierList(text.substr(plus + 1), true))
        {
            return std::nullopt;
        }
        text = text.substr(0, plus);
    }
    std::string_view prerelease;
    if (const usize dash = text.find('-'); dash != std::string_view::npos)
    {
        prerelease = text.substr(dash + 1);
        if (!isValidIdentifierList(prerelease, false))
        {
            return std::nullopt;
        }
        text = text.substr(0, dash);
    }
    const usize firstDot = text.find('.');
    const usize secondDot = firstDot == std::string_view::npos ? firstDot : text.find('.', firstDot + 1);
    if (secondDot == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::optional<u32> majorPart = parseNumber(text.substr(0, firstDot));
    const std::optional<u32> minorPart = parseNumber(text.substr(firstDot + 1, secondDot - firstDot - 1));
    const std::optional<u32> patchPart = parseNumber(text.substr(secondDot + 1));
    if (!majorPart || !minorPart || !patchPart)
    {
        return std::nullopt;
    }
    return SemanticVersion{*majorPart, *minorPart, *patchPart, std::string(prerelease)};
}

std::string SemanticVersion::toString() const
{
    std::string text = std::format("{}.{}.{}", major, minor, patch);
    if (!prerelease.empty())
    {
        text.push_back('-');
        text.append(prerelease);
    }
    return text;
}

bool SemanticVersion::isCompatibleWith(const SemanticVersion& required) const
{
    if (major != required.major || (major == 0 && minor != required.minor))
    {
        return false;
    }
    return *this >= required;
}

std::strong_ordering operator<=>(const SemanticVersion& a, const SemanticVersion& b)
{
    if (const auto order = a.major <=> b.major; order != 0)
    {
        return order;
    }
    if (const auto order = a.minor <=> b.minor; order != 0)
    {
        return order;
    }
    if (const auto order = a.patch <=> b.patch; order != 0)
    {
        return order;
    }
    // A release has higher precedence than any of its pre-releases.
    if (a.prerelease.empty() || b.prerelease.empty())
    {
        return a.prerelease.empty() <=> b.prerelease.empty();
    }
    const std::string_view left = a.prerelease;
    const std::string_view right = b.prerelease;
    usize i = 0;
    usize j = 0;
    while (i <= left.size() && j <= right.size())
    {
        const usize endLeft = std::min(left.find('.', i), left.size());
        const usize endRight = std::min(right.find('.', j), right.size());
        if (const auto order = compareIdentifier(left.substr(i, endLeft - i), right.substr(j, endRight - j));
            order != 0)
        {
            return order;
        }
        i = endLeft + 1;
        j = endRight + 1;
    }
    // Equal so far: the version with more identifiers is larger.
    return (i <= left.size()) <=> (j <= right.size());
}

const SemanticVersion& engineVersion() noexcept
{
    static const SemanticVersion version{
        ILLUSION_VERSION_MAJOR, ILLUSION_VERSION_MINOR, ILLUSION_VERSION_PATCH, {}};
    return version;
}

} // namespace illusion
