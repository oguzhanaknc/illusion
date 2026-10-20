#include <illusion/text/Utf8.hpp>

namespace illusion::text
{

namespace
{

constexpr bool isContinuation(u8 byte) noexcept
{
    return (byte & 0xC0u) == 0x80u;
}

constexpr char32_t continuationBits(u8 byte) noexcept
{
    return static_cast<char32_t>(byte & 0x3Fu);
}

} // namespace

Utf8Decoded decodeUtf8(std::string_view text, usize offset) noexcept
{
    const auto byteAt = [&](usize i) {
        return static_cast<u8>(text[offset + i]);
    };
    const usize available = text.size() - offset;
    const u8 lead = byteAt(0);
    if (lead < 0x80u)
    {
        return {static_cast<char32_t>(lead), 1, true};
    }
    const Utf8Decoded invalid{kReplacementCharacter, 1, false};
    if (lead >= 0xC2u && lead <= 0xDFu)
    {
        if (available < 2 || !isContinuation(byteAt(1)))
        {
            return invalid;
        }
        return {(static_cast<char32_t>(lead & 0x1Fu) << 6) | continuationBits(byteAt(1)), 2, true};
    }
    if (lead >= 0xE0u && lead <= 0xEFu)
    {
        if (available < 3 || !isContinuation(byteAt(1)) || !isContinuation(byteAt(2)))
        {
            return invalid;
        }
        // No overlong forms (E0 80..9F) and no surrogates (ED A0..BF).
        if ((lead == 0xE0u && byteAt(1) < 0xA0u) || (lead == 0xEDu && byteAt(1) > 0x9Fu))
        {
            return invalid;
        }
        return {(static_cast<char32_t>(lead & 0x0Fu) << 12) | (continuationBits(byteAt(1)) << 6) |
                    continuationBits(byteAt(2)),
                3, true};
    }
    if (lead >= 0xF0u && lead <= 0xF4u)
    {
        if (available < 4 || !isContinuation(byteAt(1)) || !isContinuation(byteAt(2)) ||
            !isContinuation(byteAt(3)))
        {
            return invalid;
        }
        // No overlong forms (F0 80..8F) and nothing above U+10FFFF (F4 90..BF).
        if ((lead == 0xF0u && byteAt(1) < 0x90u) || (lead == 0xF4u && byteAt(1) > 0x8Fu))
        {
            return invalid;
        }
        return {(static_cast<char32_t>(lead & 0x07u) << 18) | (continuationBits(byteAt(1)) << 12) |
                    (continuationBits(byteAt(2)) << 6) | continuationBits(byteAt(3)),
                4, true};
    }
    return invalid;
}

std::optional<usize> findInvalidUtf8(std::string_view text) noexcept
{
    usize offset = 0;
    while (offset < text.size())
    {
        if (static_cast<u8>(text[offset]) < 0x80u)
        {
            ++offset;
            continue;
        }
        const Utf8Decoded decoded = decodeUtf8(text, offset);
        if (!decoded.valid)
        {
            return offset;
        }
        offset += decoded.length;
    }
    return std::nullopt;
}

u32 encodeUtf8(char32_t codePoint, char (&out)[4]) noexcept
{
    const auto byte = [](char32_t value) {
        return static_cast<char>(static_cast<u8>(value));
    };
    if (codePoint < 0x80)
    {
        out[0] = byte(codePoint);
        return 1;
    }
    if (codePoint < 0x800)
    {
        out[0] = byte(0xC0 | (codePoint >> 6));
        out[1] = byte(0x80 | (codePoint & 0x3F));
        return 2;
    }
    if (codePoint >= 0xD800 && codePoint <= 0xDFFF)
    {
        return 0;
    }
    if (codePoint < 0x10000)
    {
        out[0] = byte(0xE0 | (codePoint >> 12));
        out[1] = byte(0x80 | ((codePoint >> 6) & 0x3F));
        out[2] = byte(0x80 | (codePoint & 0x3F));
        return 3;
    }
    if (codePoint <= 0x10FFFF)
    {
        out[0] = byte(0xF0 | (codePoint >> 18));
        out[1] = byte(0x80 | ((codePoint >> 12) & 0x3F));
        out[2] = byte(0x80 | ((codePoint >> 6) & 0x3F));
        out[3] = byte(0x80 | (codePoint & 0x3F));
        return 4;
    }
    return 0;
}

void appendUtf8(std::string& out, char32_t codePoint)
{
    char bytes[4];
    u32 length = encodeUtf8(codePoint, bytes);
    if (length == 0)
    {
        length = encodeUtf8(kReplacementCharacter, bytes);
    }
    out.append(bytes, length);
}

usize utf8Length(std::string_view text) noexcept
{
    usize count = 0;
    for (usize offset = 0; offset < text.size(); ++count)
    {
        offset += static_cast<u8>(text[offset]) < 0x80u ? 1 : decodeUtf8(text, offset).length;
    }
    return count;
}

} // namespace illusion::text
