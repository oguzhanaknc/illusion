#include <illusion/text/Utf8.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::text;
using namespace std::string_view_literals;

TEST_CASE("UTF-8 decoding reads one to four byte sequences")
{
    const std::string_view text = "A\xC3\xA7\xE2\x82\xAC\xF0\x9F\x98\x80"; // A ç € 😀
    Utf8Decoded decoded = decodeUtf8(text, 0);
    CHECK(decoded.codePoint == U'A');
    CHECK(decoded.length == 1);
    CHECK(decoded.valid);
    decoded = decodeUtf8(text, 1);
    CHECK(decoded.codePoint == U'ç');
    CHECK(decoded.length == 2);
    decoded = decodeUtf8(text, 3);
    CHECK(decoded.codePoint == U'€');
    CHECK(decoded.length == 3);
    decoded = decodeUtf8(text, 6);
    CHECK(decoded.codePoint == U'\U0001F600');
    CHECK(decoded.length == 4);
}

TEST_CASE("invalid UTF-8 decodes as one replacement character per byte")
{
    for (const std::string_view invalid :
         {"\xFF"sv, "\xC0\xAF"sv, "\xC3"sv, "\xED\xA0\x80"sv, "\xF4\x90\x80\x80"sv, "\x80"sv, "\xE2\x82"sv,
          "\xF0\x9F\x98"sv, "\xE0\x80\xAF"sv})
    {
        INFO(invalid.size());
        const Utf8Decoded decoded = decodeUtf8(invalid, 0);
        CHECK(decoded.codePoint == kReplacementCharacter);
        CHECK(decoded.length == 1);
        CHECK_FALSE(decoded.valid);
        CHECK(findInvalidUtf8(invalid) == 0u);
        CHECK_FALSE(isValidUtf8(invalid));
    }
}

TEST_CASE("findInvalidUtf8 reports the offset of the first invalid sequence")
{
    CHECK_FALSE(findInvalidUtf8("").has_value());
    CHECK_FALSE(findInvalidUtf8("plain ascii").has_value());
    CHECK_FALSE(findInvalidUtf8("\xC5\x9F\xC4\x9F\xF4\x8F\xBF\xBF").has_value()); // ş ğ U+10FFFF
    CHECK(findInvalidUtf8("ok\xC5\x9F\xFFrest") == 4u);
    CHECK(isValidUtf8("I\xC4\xB1\xC4\xB0i"));
}

TEST_CASE("UTF-8 encoding covers every length and rejects surrogates")
{
    struct Case
    {
        char32_t codePoint;
        u32 length;
    };
    for (const Case c : {Case{0x24, 1}, Case{0x7F, 1}, Case{0x80, 2}, Case{0x7FF, 2}, Case{0x800, 3},
                         Case{0xFFFF, 3}, Case{0x10000, 4}, Case{0x10FFFF, 4}})
    {
        char bytes[4];
        const u32 length = encodeUtf8(c.codePoint, bytes);
        CHECK(length == c.length);
        const Utf8Decoded decoded = decodeUtf8(std::string_view(bytes, length), 0);
        CHECK(decoded.valid);
        CHECK(decoded.codePoint == c.codePoint);
        CHECK(decoded.length == c.length);
    }
    char bytes[4];
    CHECK(encodeUtf8(0xD800, bytes) == 0);
    CHECK(encodeUtf8(0xDFFF, bytes) == 0);
    CHECK(encodeUtf8(0x110000, bytes) == 0);

    std::string out;
    appendUtf8(out, U'ş');
    appendUtf8(out, 0xD800);
    CHECK(out == "\xC5\x9F\xEF\xBF\xBD");
}

TEST_CASE("Utf8View iterates code points with their offsets")
{
    const std::string_view text = "a\xC5\x9F\xFF\xF0\x9F\x98\x80";
    std::vector<char32_t> codePoints;
    std::vector<usize> offsets;
    for (auto it = Utf8View(text).begin(); it != Utf8View(text).end(); ++it)
    {
        codePoints.push_back(*it);
        offsets.push_back(it.offset());
    }
    CHECK(codePoints == std::vector<char32_t>{U'a', U'ş', kReplacementCharacter, U'\U0001F600'});
    CHECK(offsets == std::vector<usize>{0, 1, 3, 4});
    CHECK(utf8Length(text) == 4);
    CHECK(utf8Length("") == 0);
    CHECK(Utf8View("").begin() == Utf8View("").end());
}
