#include "FontTestSupport.hpp"

#include <illusion/assets/Font.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <limits>

using namespace illusion;

namespace
{

const std::vector<std::byte>& fontFile()
{
    static const std::vector<std::byte> bytes = test::readTestFontFile();
    return bytes;
}

Font bake(std::span<const CodePointRange> ranges, u32 pixelSize = 32)
{
    FontBakeSettings settings;
    settings.pixelSize = pixelSize;
    settings.ranges = ranges;
    auto font = Font::bake(fontFile(), settings);
    REQUIRE(font.hasValue());
    return std::move(font).value();
}

template <class T>
void overwrite(std::vector<std::byte>& bytes, usize offset, T value)
{
    REQUIRE(offset + sizeof(T) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

// Offsets in the serialized form (see serializeFont()).
constexpr usize kFallbackOffset = 36;
constexpr usize kGlyphCountOffset = 40;
constexpr usize kFirstGlyphOffset = 48;
constexpr usize kGlyphSize = 40;

} // namespace

TEST_CASE("the test font file is available")
{
    REQUIRE(fontFile().size() > 1000);
}

TEST_CASE("an empty font has no glyphs and no kerning")
{
    const Font font;
    CHECK(font.empty());
    CHECK(font.glyphs().empty());
    CHECK(font.find(U'A') == nullptr);
    CHECK(font.kerning(U'A', U'V') == 0.0f);
    CHECK(font.atlas().empty());
}

TEST_CASE("baking the same font twice gives the same bytes")
{
    const auto first = Font::bake(fontFile());
    const auto second = Font::bake(fontFile());
    REQUIRE(first.hasValue());
    REQUIRE(second.hasValue());
    CHECK(serializeFont(*first) == serializeFont(*second));
    CHECK(first->pixelSize() == 48);
    CHECK(first->spread() == 6);
    CHECK(first->find(U'ş') != nullptr);
    CHECK(first->find(U'€') != nullptr);
}

TEST_CASE("a baked font has sorted glyphs inside a power of two atlas")
{
    const Font& font = test::testFont();
    REQUIRE(font.glyphs().size() == 95);
    const UVec2 atlas = font.atlasSize();
    CHECK(atlas.x == atlas.y);
    CHECK((atlas.x & (atlas.x - 1)) == 0);
    CHECK(font.atlas().size() == usize{atlas.x} * atlas.y);
    CHECK(font.metrics().ascent > 0.5f);
    CHECK(font.metrics().descent > 0.1f);
    CHECK(font.metrics().lineHeight() > 1.0f);
    for (usize i = 1; i < font.glyphs().size(); ++i)
    {
        CHECK(font.glyphs()[i - 1].codePoint < font.glyphs()[i].codePoint);
    }
    for (const FontGlyph& glyph : font.glyphs())
    {
        CHECK(glyph.advance >= 0.0f);
        CHECK(glyph.uvMin.x >= 0.0f);
        CHECK(glyph.uvMax.x <= 1.0f);
        CHECK(glyph.uvMin.y >= 0.0f);
        CHECK(glyph.uvMax.y <= 1.0f);
        // A quad covers as many atlas texels as it is long in ems times the pixel size.
        const Vec2 texels = (glyph.uvMax - glyph.uvMin) * Vec2(atlas);
        const Vec2 ems = (glyph.planeMax - glyph.planeMin) * static_cast<f32>(font.pixelSize());
        CHECK(static_cast<f64>(texels.x) == doctest::Approx(static_cast<f64>(ems.x)).epsilon(1e-4));
        CHECK(static_cast<f64>(texels.y) == doctest::Approx(static_cast<f64>(ems.y)).epsilon(1e-4));
    }
}

TEST_CASE("glyphs with ink do not overlap in the atlas and have a field around their edges")
{
    const Font& font = test::testFont();
    const Vec2 atlas(font.atlasSize());
    std::vector<std::pair<Vec2, Vec2>> boxes;
    for (const FontGlyph& glyph : font.glyphs())
    {
        if (glyph.planeMin != glyph.planeMax)
        {
            boxes.emplace_back(glyph.uvMin * atlas, glyph.uvMax * atlas);
        }
    }
    for (usize a = 0; a < boxes.size(); ++a)
    {
        for (usize b = a + 1; b < boxes.size(); ++b)
        {
            const bool apart = boxes[a].second.x <= boxes[b].first.x ||
                               boxes[b].second.x <= boxes[a].first.x ||
                               boxes[a].second.y <= boxes[b].first.y || boxes[b].second.y <= boxes[a].first.y;
            CHECK(apart);
        }
    }
    // The centre of the bar of 'l' is inside the glyph, the corner of its quad far outside.
    const FontGlyph* l = font.find(U'l');
    REQUIRE(l != nullptr);
    const UVec2 min(l->uvMin * atlas);
    const UVec2 max(l->uvMax * atlas);
    const u32 width = font.atlasSize().x;
    CHECK(font.atlas()[usize{(min.y + max.y) / 2} * width + (min.x + max.x) / 2] > 128);
    CHECK(font.atlas()[usize{min.y} * width + min.x] < 32);
}

TEST_CASE("glyphs without ink take no atlas space")
{
    const Font& font = test::testFont();
    const FontGlyph* space = font.find(U' ');
    REQUIRE(space != nullptr);
    CHECK(space->planeMin == space->planeMax);
    CHECK(space->uvMin == space->uvMax);
    CHECK(space->advance > 0.1f);
    const FontGlyph* a = font.find(U'A');
    REQUIRE(a != nullptr);
    CHECK(a->planeMin.x < a->planeMax.x);
    CHECK(a->planeMin.y < 0.0f); // above the baseline, y down
}

TEST_CASE("code points the font lacks are left out")
{
    static constexpr CodePointRange kRanges[] = {{U'A', U'C'}, {0x0E01, 0x0E05}, {U'B', U'D'}};
    const Font font = bake(kRanges);
    REQUIRE(font.glyphs().size() == 4);
    CHECK(font.glyphs()[0].codePoint == U'A');
    CHECK(font.glyphs()[3].codePoint == U'D');
    CHECK(font.find(0x0E01) == nullptr);
}

TEST_CASE("a font without any requested code point is not baked")
{
    static constexpr CodePointRange kThai[] = {{0x0E01, 0x0E2E}};
    FontBakeSettings settings;
    settings.ranges = kThai;
    const auto font = Font::bake(fontFile(), settings);
    REQUIRE_FALSE(font.hasValue());
    CHECK(font.error() == FontError::NoGlyphs);
    settings.ranges = {};
    CHECK(Font::bake(fontFile(), settings).error() == FontError::NoGlyphs);
}

TEST_CASE("missing glyphs fall back to the replacement character, then '?', then the first glyph")
{
    static constexpr CodePointRange kWithReplacement[] = {{U'?', U'?'}, {U'A', U'B'}, {0xFFFD, 0xFFFD}};
    const Font withReplacement = bake(kWithReplacement);
    REQUIRE(withReplacement.find(0xFFFD) != nullptr);
    CHECK(withReplacement.glyphOrFallback(0x0E01).codePoint == 0xFFFD);
    CHECK(withReplacement.glyphOrFallback(U'B').codePoint == U'B');

    static constexpr CodePointRange kWithQuestionMark[] = {{U'?', U'?'}, {U'A', U'B'}};
    CHECK(bake(kWithQuestionMark).glyphOrFallback(0x0E01).codePoint == U'?');

    static constexpr CodePointRange kLetters[] = {{U'X', U'Z'}};
    CHECK(bake(kLetters).glyphOrFallback(0x0E01).codePoint == U'X');
}

TEST_CASE("kerning pairs are found among the baked glyphs")
{
    const Font& font = test::testFont();
    CHECK(font.kerning(U'A', U'V') < 0.0f);
    CHECK(font.kerning(U'T', U'o') < 0.0f);
    CHECK(font.kerning(U'V', U'A') < 0.0f);
    CHECK(font.kerning(U'l', U'l') == 0.0f);
    CHECK(font.kerning(U'A', 0x0E01) == 0.0f);

    static constexpr CodePointRange kOnlyA[] = {{U'A', U'A'}};
    CHECK(bake(kOnlyA).kerning(U'A', U'V') == 0.0f);
}

TEST_CASE("glyphs that do not fit into the largest atlas fail with AtlasFull")
{
    FontBakeSettings settings;
    settings.maxAtlasSize = 256;
    const auto font = Font::bake(fontFile(), settings);
    REQUIRE_FALSE(font.hasValue());
    CHECK(font.error() == FontError::AtlasFull);
}

TEST_CASE("settings outside their ranges are rejected")
{
    const auto rejected = [](const FontBakeSettings& settings) {
        const auto font = Font::bake(fontFile(), settings);
        return !font.hasValue() && font.error() == FontError::InvalidSettings;
    };
    FontBakeSettings settings;
    settings.pixelSize = 15;
    CHECK(rejected(settings));
    settings.pixelSize = 129;
    CHECK(rejected(settings));
    settings = {};
    settings.spread = 1;
    CHECK(rejected(settings));
    settings.spread = 17;
    CHECK(rejected(settings));
    settings = {};
    settings.maxAtlasSize = 255;
    CHECK(rejected(settings));
    settings.maxAtlasSize = 8193;
    CHECK(rejected(settings));
    static constexpr CodePointRange kInverted[] = {{U'B', U'A'}};
    settings = {};
    settings.ranges = kInverted;
    CHECK(rejected(settings));
    static constexpr CodePointRange kBeyondUnicode[] = {{0x10FFFF, 0x110000}};
    settings.ranges = kBeyondUnicode;
    CHECK(rejected(settings));
}

TEST_CASE("bytes that are not a font are rejected")
{
    const auto invalid = [](std::span<const std::byte> bytes) {
        const auto font = Font::bake(bytes);
        return !font.hasValue() && font.error() == FontError::InvalidFont;
    };
    CHECK(invalid({}));
    std::vector<std::byte> noise(4096);
    for (usize i = 0; i < noise.size(); ++i)
    {
        noise[i] = static_cast<std::byte>((i * 7919u + 13u) & 0xFFu);
    }
    CHECK(invalid(noise));
    // A TrueType header whose table directory runs past the end.
    overwrite(noise, 0, u32{0x00000100});
    CHECK(invalid(noise));
    const std::vector<std::byte>& file = fontFile();
    for (const usize cut : {usize{4}, usize{12}, usize{64}, usize{400}, file.size() / 4, file.size() / 2})
    {
        INFO("cut at " << cut);
        CHECK(invalid(std::span(file).first(cut)));
    }
}

TEST_CASE("a font survives serialization unchanged")
{
    const Font& font = test::testFont();
    const std::vector<std::byte> bytes = serializeFont(font);
    const std::optional<Font> copy = deserializeFont(bytes);
    REQUIRE(copy.has_value());
    CHECK(serializeFont(*copy) == bytes);
    CHECK(copy->glyphs().size() == font.glyphs().size());
    CHECK(copy->atlasSize() == font.atlasSize());
    CHECK(copy->pixelSize() == font.pixelSize());
    CHECK(copy->spread() == font.spread());
    CHECK(copy->kerning(U'A', U'V') == font.kerning(U'A', U'V'));
    CHECK(copy->glyphOrFallback(0x0E01).codePoint == font.glyphOrFallback(0x0E01).codePoint);
    CHECK(static_cast<f64>(copy->metrics().ascent) == static_cast<f64>(font.metrics().ascent));
}

TEST_CASE("truncated font data is rejected at every cut point")
{
    const std::vector<std::byte> bytes = serializeFont(test::testFont());
    for (usize cut = 0; cut < bytes.size(); cut += (cut < 4096 ? 1 : 997))
    {
        INFO("cut at " << cut);
        CHECK_FALSE(deserializeFont(std::span(bytes).first(cut)).has_value());
    }
    std::vector<std::byte> longer = bytes;
    longer.push_back(std::byte{0});
    CHECK_FALSE(deserializeFont(longer).has_value());
}

TEST_CASE("inconsistent font data is rejected")
{
    const std::vector<std::byte> bytes = serializeFont(test::testFont());
    REQUIRE(deserializeFont(bytes).has_value());
    const auto rejectedWith = [&](auto change) {
        std::vector<std::byte> corrupted = bytes;
        change(corrupted);
        return !deserializeFont(corrupted).has_value();
    };
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, 0, u32{0}); }));  // magic
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, 4, u32{2}); }));  // version
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, 8, u32{8}); }));  // pixel size
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, 12, u32{0}); })); // spread
    CHECK(rejectedWith(
        [](std::vector<std::byte>& b) { overwrite(b, 16, std::numeric_limits<f32>::infinity()); }));
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, 28, u32{0}); })); // atlas width
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, 28, u32{16384}); }));
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, kFallbackOffset, u32{95}); }));
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, kGlyphCountOffset, u64{0}); }));
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, kGlyphCountOffset, ~u64{0}); }));
    // Glyphs out of order, a code point beyond Unicode, a non-finite advance, a uv outside the atlas.
    CHECK(rejectedWith(
        [](std::vector<std::byte>& b) { overwrite(b, kFirstGlyphOffset + kGlyphSize, u32{0x20}); }));
    CHECK(rejectedWith(
        [](std::vector<std::byte>& b) { overwrite(b, kFirstGlyphOffset + 94 * kGlyphSize, u32{0x110000}); }));
    CHECK(rejectedWith([](std::vector<std::byte>& b) {
        overwrite(b, kFirstGlyphOffset + 4, std::numeric_limits<f32>::quiet_NaN());
    }));
    CHECK(rejectedWith([](std::vector<std::byte>& b) { overwrite(b, kFirstGlyphOffset + 24, 1.5f); }));
    // The planeMax of 'A' before its planeMin.
    CHECK(rejectedWith(
        [](std::vector<std::byte>& b) { overwrite(b, kFirstGlyphOffset + 33 * kGlyphSize + 16, -10.0f); }));
}
