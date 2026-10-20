#include <illusion/text/LocaleId.hpp>

namespace illusion::text
{

namespace
{

constexpr bool isLetter(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

constexpr char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char upper(char c) noexcept
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool allLetters(std::string_view part) noexcept
{
    for (const char c : part)
    {
        if (!isLetter(c))
        {
            return false;
        }
    }
    return true;
}

bool allDigits(std::string_view part) noexcept
{
    for (const char c : part)
    {
        if (!isDigit(c))
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<LocaleId> LocaleId::parse(std::string_view tag) noexcept
{
    std::string_view parts[3];
    usize count = 0;
    usize start = 0;
    for (usize i = 0; i <= tag.size(); ++i)
    {
        if (i == tag.size() || tag[i] == '-' || tag[i] == '_')
        {
            if (count == 3)
            {
                return std::nullopt;
            }
            parts[count++] = tag.substr(start, i - start);
            start = i + 1;
        }
    }

    const std::string_view language = parts[0];
    if (language.size() < 2 || language.size() > 3 || !allLetters(language))
    {
        return std::nullopt;
    }
    std::string_view script;
    std::string_view region;
    for (usize i = 1; i < count; ++i)
    {
        const std::string_view part = parts[i];
        if (part.size() == 4 && allLetters(part) && script.empty() && region.empty())
        {
            script = part;
        }
        else if (((part.size() == 2 && allLetters(part)) || (part.size() == 3 && allDigits(part))) &&
                 region.empty())
        {
            region = part;
        }
        else
        {
            return std::nullopt;
        }
    }

    LocaleId id;
    usize length = 0;
    for (const char c : language)
    {
        id.m_tag[length++] = lower(c);
    }
    id.m_languageLength = static_cast<u8>(language.size());
    if (!script.empty())
    {
        id.m_tag[length++] = '-';
        for (usize i = 0; i < script.size(); ++i)
        {
            id.m_tag[length++] = i == 0 ? upper(script[i]) : lower(script[i]);
        }
        id.m_scriptLength = 4;
    }
    if (!region.empty())
    {
        id.m_tag[length++] = '-';
        for (const char c : region)
        {
            id.m_tag[length++] = upper(c);
        }
        id.m_regionLength = static_cast<u8>(region.size());
    }
    id.m_length = static_cast<u8>(length);
    return id;
}

std::string_view LocaleId::script() const noexcept
{
    return m_scriptLength == 0 ? std::string_view() : tag().substr(m_languageLength + 1u, m_scriptLength);
}

std::string_view LocaleId::region() const noexcept
{
    return m_regionLength == 0 ? std::string_view() : tag().substr(m_length - m_regionLength);
}

std::optional<LocaleId> LocaleId::parent() const noexcept
{
    if (m_regionLength == 0 && m_scriptLength == 0)
    {
        return std::nullopt;
    }
    LocaleId parent = *this;
    if (m_regionLength != 0)
    {
        parent.m_length = static_cast<u8>(m_length - m_regionLength - 1);
        parent.m_regionLength = 0;
    }
    else
    {
        parent.m_length = m_languageLength;
        parent.m_scriptLength = 0;
    }
    for (usize i = parent.m_length; i < kMaxLength; ++i)
    {
        parent.m_tag[i] = '\0';
    }
    return parent;
}

} // namespace illusion::text
