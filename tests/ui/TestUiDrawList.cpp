#include "FontTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/ui/UiDrawList.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace illusion;

namespace
{

constexpr UiTextureId kPanel{7};
constexpr UiTextureId kAtlas{9};
constexpr f32 kNaN = std::numeric_limits<f32>::quiet_NaN();

UiCanvasMetrics hdCanvas()
{
    return canvasMetrics({}, {1920, 1080});
}

UiFontFace face()
{
    return {&test::testFont(), kAtlas};
}

bool near(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}

bool near(Vec2 a, Vec2 b, f32 tolerance = 1e-4f)
{
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance);
}

// The quads of a list: positions of their top left and bottom right vertices.
struct DrawnQuad
{
    Vec2 min;
    Vec2 max;
    Vec2 uvMin;
    Vec2 uvMax;
};

DrawnQuad quadAt(const UiDrawData& data, usize quad)
{
    const UiVertex& topLeft = data.vertices[quad * 4];
    const UiVertex& bottomRight = data.vertices[quad * 4 + 2];
    return {topLeft.position, bottomRight.position, topLeft.uv, bottomRight.uv};
}

f32 textWidth(std::string_view utf8, f32 size = 32.0f)
{
    UiTextStyle style;
    style.size = size;
    return measureText(test::testFont(), utf8, style).size.x;
}

} // namespace

TEST_CASE("a new draw list is empty with the whole canvas as its clip")
{
    UiDrawList list;
    list.begin(hdCanvas());
    const UiDrawData data = list.data();
    CHECK(data.vertices.empty());
    CHECK(data.indices.empty());
    CHECK(data.commands.empty());
    CHECK(list.clip() == UiRect{{0.0f, 0.0f}, {1920.0f, 1080.0f}});
    CHECK(data.canvas.pixelsPerUnit == 1.0f);
    CHECK(list.droppedPrimitives() == 0);
    CHECK(list.missingGlyphs() == 0);
}

TEST_CASE("a rectangle is one solid quad with its colour encoded as sRGB")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.rect({{10.0f, 20.0f}, {110.0f, 70.0f}}, {0.5f, 0.0f, 1.0f, 0.6f}));
    const UiDrawData data = list.data();
    REQUIRE(data.vertices.size() == 4);
    REQUIRE(data.indices.size() == 6);
    REQUIRE(data.commands.size() == 1);
    CHECK(data.vertices[0].position == Vec2(10.0f, 20.0f));
    CHECK(data.vertices[1].position == Vec2(110.0f, 20.0f));
    CHECK(data.vertices[2].position == Vec2(110.0f, 70.0f));
    CHECK(data.vertices[3].position == Vec2(10.0f, 70.0f));
    // Linear 0.5 is sRGB 188; alpha stays linear and straight: 0.6 * 255 = 153.
    for (const UiVertex& vertex : data.vertices)
    {
        CHECK(vertex.color == (188u | (0u << 8u) | (255u << 16u) | (153u << 24u)));
    }
    const u32 expected[] = {0, 1, 2, 0, 2, 3};
    for (usize i = 0; i < 6; ++i)
    {
        CHECK(data.indices[i] == expected[i]);
    }
    const UiDrawCommand& command = data.commands[0];
    CHECK(command.texture == UiTextureId{});
    CHECK(command.sdfSpread == 0.0f);
    CHECK(command.clip == list.clip());
    CHECK(command.firstIndex == 0);
    CHECK(command.indexCount == 6);
}

TEST_CASE("colours are clamped and encoded exactly")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.rect({{0.0f, 0.0f}, {1.0f, 1.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}));
    REQUIRE(list.rect({{0.0f, 0.0f}, {1.0f, 1.0f}}, {2.0f, -1.0f, 0.0f, 3.0f}));
    REQUIRE(list.rect({{0.0f, 0.0f}, {1.0f, 1.0f}}, {0.0031308f, 0.2140f, 0.2159f, 0.0f}));
    const UiDrawData data = list.data();
    CHECK(data.vertices[0].color == 0xFFFFFFFFu);
    CHECK(data.vertices[4].color == 0xFF0000FFu);
    // 0.0031308 encodes to 0.04045 (10.3 -> 10); 0.2140 and 0.2159 straddle sRGB 127.5.
    CHECK((data.vertices[8].color & 0xFFu) == 10u);
    CHECK(((data.vertices[8].color >> 8u) & 0xFFu) == 127u);
    CHECK(((data.vertices[8].color >> 16u) & 0xFFu) == 128u);
    CHECK((data.vertices[8].color >> 24u) == 0u);
}

TEST_CASE("colour encoding follows the sRGB curve across the whole range")
{
    UiDrawList list;
    list.begin(hdCanvas());
    constexpr int kSteps = 2000;
    for (int i = 0; i <= kSteps; ++i)
    {
        const f32 linear = static_cast<f32>(i) / static_cast<f32>(kSteps);
        REQUIRE(list.rect({{0.0f, 0.0f}, {1.0f, 1.0f}}, {linear, 0.0f, 0.0f, linear}));
    }
    const UiDrawData data = list.data();
    for (int i = 0; i <= kSteps; ++i)
    {
        const f64 linear = static_cast<f64>(static_cast<f32>(i) / static_cast<f32>(kSteps));
        const f64 encoded =
            linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        const u32 color = data.vertices[static_cast<usize>(i) * 4].color;
        INFO("linear " << linear);
        CHECK((color & 0xFFu) == static_cast<u32>(std::lround(encoded * 255.0)));
        CHECK((color >> 24u) == static_cast<u32>(std::lround(linear * 255.0)));
    }
}

TEST_CASE("an image maps its uv rectangle onto its corners")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.image({{0.0f, 0.0f}, {64.0f, 32.0f}}, kPanel, Vec4(1.0f), {{0.25f, 0.5f}, {0.75f, 1.0f}}));
    const UiDrawData data = list.data();
    REQUIRE(data.vertices.size() == 4);
    CHECK(data.vertices[0].uv == Vec2(0.25f, 0.5f));
    CHECK(data.vertices[1].uv == Vec2(0.75f, 0.5f));
    CHECK(data.vertices[2].uv == Vec2(0.75f, 1.0f));
    CHECK(data.vertices[3].uv == Vec2(0.25f, 1.0f));
    REQUIRE(data.commands.size() == 1);
    CHECK(data.commands[0].texture == kPanel);
    CHECK(data.commands[0].sdfSpread == 0.0f);
}

TEST_CASE("empty rectangles record nothing and succeed")
{
    UiDrawList list;
    list.begin(hdCanvas());
    CHECK(list.rect({{10.0f, 10.0f}, {10.0f, 50.0f}}, Vec4(1.0f)));
    CHECK(list.image({{10.0f, 10.0f}, {5.0f, 50.0f}}, kPanel));
    CHECK(list.nineSlice({{10.0f, 10.0f}, {50.0f, 10.0f}}, kPanel, {32.0f, 32.0f}, {8.0f, 8.0f, 8.0f, 8.0f}));
    CHECK(list.data().vertices.empty());
    CHECK(list.data().commands.empty());
    CHECK(list.droppedPrimitives() == 0);
}

TEST_CASE("consecutive primitives with the same texture and clip share a command")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.rect({{0.0f, 0.0f}, {10.0f, 10.0f}}, Vec4(1.0f)));
    REQUIRE(list.rect({{20.0f, 0.0f}, {30.0f, 10.0f}}, Vec4(0.5f)));
    REQUIRE(list.image({{0.0f, 20.0f}, {10.0f, 30.0f}}, kPanel));
    REQUIRE(list.image({{20.0f, 20.0f}, {30.0f, 30.0f}}, kPanel));
    REQUIRE(list.pushClip({{0.0f, 0.0f}, {100.0f, 100.0f}}));
    REQUIRE(list.image({{40.0f, 20.0f}, {50.0f, 30.0f}}, kPanel));
    list.popClip();
    REQUIRE(list.image({{60.0f, 20.0f}, {70.0f, 30.0f}}, kPanel));
    REQUIRE(list.rect({{0.0f, 0.0f}, {10.0f, 10.0f}}, Vec4(1.0f)));

    const UiDrawData data = list.data();
    REQUIRE(data.commands.size() == 5);
    CHECK(data.commands[0].indexCount == 12);
    CHECK(data.commands[1].texture == kPanel);
    CHECK(data.commands[1].firstIndex == 12);
    CHECK(data.commands[1].indexCount == 12);
    CHECK(data.commands[2].clip == UiRect{{0.0f, 0.0f}, {100.0f, 100.0f}});
    CHECK(data.commands[3].clip == UiRect{{0.0f, 0.0f}, {1920.0f, 1080.0f}});
    CHECK(data.commands[4].texture == UiTextureId{});
    u32 indices = 0;
    for (const UiDrawCommand& command : data.commands)
    {
        CHECK(command.firstIndex == indices);
        CHECK(command.indexCount % 6 == 0);
        indices += command.indexCount;
    }
    CHECK(indices == data.indices.size());
    // The second quad's indices point at its own vertices.
    CHECK(data.indices[6] == 4);
    CHECK(data.indices[11] == 7);
}

TEST_CASE("the clip stack narrows, restores and refuses to overflow")
{
    UiDrawListDesc desc;
    desc.maxClipDepth = 2;
    UiDrawList list(desc);
    list.begin(hdCanvas());
    REQUIRE(list.pushClip({{100.0f, 100.0f}, {500.0f, 400.0f}}));
    CHECK(list.clip() == UiRect{{100.0f, 100.0f}, {500.0f, 400.0f}});
    REQUIRE(list.pushClip({{300.0f, -50.0f}, {900.0f, 200.0f}}));
    CHECK(list.clip() == UiRect{{300.0f, 100.0f}, {500.0f, 200.0f}});
    CHECK_FALSE(list.pushClip({{0.0f, 0.0f}, {10.0f, 10.0f}}));
    CHECK(list.clip() == UiRect{{300.0f, 100.0f}, {500.0f, 200.0f}});
    list.popClip();
    CHECK(list.clip() == UiRect{{100.0f, 100.0f}, {500.0f, 400.0f}});
    list.popClip();
    CHECK(list.clip() == UiRect{{0.0f, 0.0f}, {1920.0f, 1080.0f}});

    REQUIRE(list.pushClip({{2000.0f, 0.0f}, {2100.0f, 10.0f}}));
    CHECK(list.clip().empty());
    REQUIRE(list.pushClip({{kNaN, 0.0f}, {10.0f, 10.0f}}));
    CHECK(list.clip().empty());
    list.begin(canvasMetrics({}, {3840, 2160}));
    CHECK(list.clip() == UiRect{{0.0f, 0.0f}, {1920.0f, 1080.0f}});
    REQUIRE(list.pushClip({{0.0f, 0.0f}, {1.0f, 1.0f}}));
}

TEST_CASE("a full list drops whole primitives and counts them")
{
    UiDrawListDesc desc;
    desc.maxVertices = 16;
    desc.maxIndices = 24;
    desc.maxCommands = 2;
    UiDrawList list(desc);
    list.begin(hdCanvas());
    REQUIRE(list.rect({{0.0f, 0.0f}, {10.0f, 10.0f}}, Vec4(1.0f)));
    // Nine quads do not fit into the twelve vertices left: none of them is recorded.
    CHECK_FALSE(
        list.nineSlice({{0.0f, 0.0f}, {100.0f, 100.0f}}, kPanel, {32.0f, 32.0f}, {8.0f, 8.0f, 8.0f, 8.0f}));
    CHECK(list.data().vertices.size() == 4);
    CHECK(list.data().commands.size() == 1);
    CHECK(list.droppedPrimitives() == 1);
    REQUIRE(list.image({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel));
    // A third texture needs a third command.
    CHECK_FALSE(list.image({{0.0f, 0.0f}, {10.0f, 10.0f}}, UiTextureId{3}));
    CHECK(list.droppedPrimitives() == 2);
    REQUIRE(list.image({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel));
    REQUIRE(list.image({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel));
    CHECK_FALSE(list.image({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel));
    CHECK(list.data().vertices.size() == 16);
    CHECK(list.data().indices.size() == 24);
    CHECK(list.droppedPrimitives() == 3);
    CHECK_FALSE(list.text(face(), {0.0f, 0.0f}, "Hello", {}));
    CHECK(list.droppedPrimitives() == 4);

    list.begin(hdCanvas());
    CHECK(list.droppedPrimitives() == 0);
    CHECK(list.rect({{0.0f, 0.0f}, {10.0f, 10.0f}}, Vec4(1.0f)));
}

TEST_CASE("primitives with non-finite values are dropped")
{
    UiDrawList list;
    list.begin(hdCanvas());
    CHECK_FALSE(list.rect({{kNaN, 0.0f}, {10.0f, 10.0f}}, Vec4(1.0f)));
    CHECK_FALSE(
        list.rect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1.0f, std::numeric_limits<f32>::infinity(), 0.0f, 1.0f}));
    CHECK_FALSE(list.image({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel, Vec4(1.0f), {{0.0f, kNaN}, {1.0f, 1.0f}}));
    CHECK_FALSE(list.nineSlice({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel, {0.0f, 32.0f}, {}));
    CHECK_FALSE(
        list.nineSlice({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel, {32.0f, 32.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}));
    CHECK_FALSE(list.nineSlice({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel, {32.0f, 32.0f}, {}, Vec4(1.0f), kNaN));
    UiTextStyle style;
    style.size = kNaN;
    CHECK_FALSE(list.text(face(), {0.0f, 0.0f}, "A", style));
    style.size = 0.0f;
    CHECK_FALSE(list.text(face(), {0.0f, 0.0f}, "A", style));
    CHECK_FALSE(list.text(face(), {kNaN, 0.0f}, "A", {}));
    CHECK_FALSE(list.text({nullptr, kAtlas}, {0.0f, 0.0f}, "A", {}));
    const Font empty;
    CHECK_FALSE(list.text({&empty, kAtlas}, {0.0f, 0.0f}, "A", {}));

    UiTransform2D broken;
    broken.x = {kNaN, 0.0f};
    list.setTransform(broken);
    CHECK_FALSE(list.rect({{0.0f, 0.0f}, {10.0f, 10.0f}}, Vec4(1.0f)));
    CHECK(list.droppedPrimitives() == 12);
    CHECK(list.data().vertices.empty());
    CHECK(list.data().commands.empty());
}

TEST_CASE("the transform moves the corners of every later primitive")
{
    UiDrawList list;
    list.begin(hdCanvas());
    list.setTransform(makeUiTransform({50.0f, 50.0f}, 0.0f, {2.0f, 2.0f}));
    REQUIRE(list.rect({{40.0f, 40.0f}, {60.0f, 60.0f}}, Vec4(1.0f)));
    list.setTransform({});
    REQUIRE(list.rect({{40.0f, 40.0f}, {60.0f, 60.0f}}, Vec4(1.0f)));
    const UiDrawData data = list.data();
    CHECK(near(data.vertices[0].position, {30.0f, 30.0f}));
    CHECK(near(data.vertices[2].position, {70.0f, 70.0f}));
    CHECK(data.vertices[4].position == Vec2(40.0f, 40.0f));
    // A rotated quad keeps its four distinct corners.
    list.begin(hdCanvas());
    list.setTransform(makeUiTransform({50.0f, 50.0f}, 0.5f, {1.0f, 1.0f}));
    REQUIRE(list.rect({{40.0f, 40.0f}, {60.0f, 60.0f}}, Vec4(1.0f)));
    CHECK(list.data().vertices[1].position.y > 40.0f);
}

TEST_CASE("a nine-slice panel keeps its corners and stretches its edges and centre")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(
        list.nineSlice({{0.0f, 0.0f}, {100.0f, 50.0f}}, kPanel, {32.0f, 32.0f}, {8.0f, 4.0f, 8.0f, 12.0f}));
    const UiDrawData data = list.data();
    REQUIRE(data.vertices.size() == 36);
    REQUIRE(data.indices.size() == 54);
    REQUIRE(data.commands.size() == 1);
    const f32 xs[] = {0.0f, 8.0f, 92.0f, 100.0f};
    const f32 ys[] = {0.0f, 4.0f, 38.0f, 50.0f};
    const f32 us[] = {0.0f, 0.25f, 0.75f, 1.0f};
    const f32 vs[] = {0.0f, 0.125f, 0.625f, 1.0f};
    for (usize row = 0; row < 3; ++row)
    {
        for (usize column = 0; column < 3; ++column)
        {
            const DrawnQuad quad = quadAt(data, row * 3 + column);
            CHECK(near(quad.min, {xs[column], ys[row]}));
            CHECK(near(quad.max, {xs[column + 1], ys[row + 1]}));
            CHECK(near(quad.uvMin, {us[column], vs[row]}));
            CHECK(near(quad.uvMax, {us[column + 1], vs[row + 1]}));
        }
    }
}

TEST_CASE("nine-slice borders scale with borderScale and shrink to fit a small rectangle")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.nineSlice({{0.0f, 0.0f}, {100.0f, 100.0f}}, kPanel, {32.0f, 32.0f}, {8.0f, 8.0f, 8.0f, 8.0f},
                           Vec4(1.0f), 2.0f));
    CHECK(near(quadAt(list.data(), 0).max, {16.0f, 16.0f}));

    // 16 + 16 units of border in 10: both shrink to 5 and the centre row and column vanish.
    list.begin(hdCanvas());
    REQUIRE(list.nineSlice({{0.0f, 0.0f}, {10.0f, 10.0f}}, kPanel, {32.0f, 32.0f}, {8.0f, 8.0f, 8.0f, 8.0f},
                           Vec4(1.0f), 2.0f));
    REQUIRE(list.data().vertices.size() == 16);
    const DrawnQuad topLeft = quadAt(list.data(), 0);
    CHECK(near(topLeft.max, {5.0f, 5.0f}));
    CHECK(near(topLeft.uvMax, {0.25f, 0.25f}));
    const DrawnQuad bottomRight = quadAt(list.data(), 3);
    CHECK(near(bottomRight.min, {5.0f, 5.0f}));
    CHECK(near(bottomRight.uvMin, {0.75f, 0.75f}));

    // Unequal borders shrink in proportion.
    list.begin(hdCanvas());
    REQUIRE(
        list.nineSlice({{0.0f, 0.0f}, {10.0f, 100.0f}}, kPanel, {32.0f, 32.0f}, {12.0f, 0.0f, 4.0f, 0.0f}));
    CHECK(near(quadAt(list.data(), 0).max.x, 7.5f));
}

TEST_CASE("a nine-slice panel without borders is one stretched quad")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.nineSlice({{0.0f, 0.0f}, {100.0f, 50.0f}}, kPanel, {32.0f, 32.0f}, {}));
    REQUIRE(list.data().vertices.size() == 4);
    CHECK(quadAt(list.data(), 0).uvMin == Vec2(0.0f));
    CHECK(quadAt(list.data(), 0).uvMax == Vec2(1.0f));
    list.begin(hdCanvas());
    REQUIRE(
        list.nineSlice({{0.0f, 0.0f}, {100.0f, 50.0f}}, kPanel, {32.0f, 32.0f}, {8.0f, 0.0f, 0.0f, 0.0f}));
    CHECK(list.data().vertices.size() == 8);
}

TEST_CASE("text draws one quad per glyph with ink in a distance field command")
{
    UiDrawList list;
    list.begin(hdCanvas());
    UiTextStyle style;
    style.size = 32.0f;
    REQUIRE(list.text(face(), {100.0f, 100.0f}, "Hi there", style));
    const UiDrawData data = list.data();
    CHECK(data.vertices.size() == 7 * 4);
    REQUIRE(data.commands.size() == 1);
    CHECK(data.commands[0].texture == kAtlas);
    CHECK(data.commands[0].sdfSpread == static_cast<f32>(test::testFont().spread()));
    CHECK(list.missingGlyphs() == 0);
    // The first glyph's quad sits on the first baseline.
    const FontGlyph& h = test::testFont().glyphOrFallback(U'H');
    const f32 baseline = 100.0f + test::testFont().metrics().ascent * 32.0f;
    const DrawnQuad first = quadAt(data, 0);
    CHECK(near(first.min.y, std::round(baseline) + h.planeMin.y * 32.0f, 1e-3f));
    CHECK(first.uvMin == h.uvMin);
    CHECK(first.uvMax == h.uvMax);
    CHECK(list.text(face(), {0.0f, 0.0f}, "", style));
    CHECK(list.text(face(), {0.0f, 0.0f}, "   ", style));
    CHECK(list.data().vertices.size() == 7 * 4);
}

TEST_CASE("glyph origins snap to whole pixels only without a transform")
{
    UiDrawList list;
    UiCanvasScale scale;
    scale.mode = UiScaleMode::Pixels;
    scale.userScale = 2.0f;
    list.begin(canvasMetrics(scale, {1920, 1080}));
    UiTextStyle style;
    style.size = 17.3f;
    REQUIRE(list.text(face(), {10.3f, 20.1f}, "AVATAR", style));
    const Font& font = test::testFont();
    usize quad = 0;
    for (const char c : std::string_view("AVATAR"))
    {
        const FontGlyph& glyph = font.glyphOrFallback(static_cast<char32_t>(c));
        const Vec2 origin = quadAt(list.data(), quad++).min - glyph.planeMin * style.size;
        const Vec2 pixels = origin * 2.0f;
        CHECK(near(pixels.x, std::round(pixels.x), 1e-3f));
        CHECK(near(pixels.y, std::round(pixels.y), 1e-3f));
    }
    list.begin(canvasMetrics(scale, {1920, 1080}));
    UiTransform2D shifted;
    shifted.translation = {0.25f, 0.0f};
    list.setTransform(shifted);
    REQUIRE(list.text(face(), {10.3f, 20.1f}, "A", style));
    const FontGlyph& a = font.glyphOrFallback(U'A');
    CHECK(near(quadAt(list.data(), 0).min.x, 10.55f + a.planeMin.x * style.size, 1e-3f));
}

TEST_CASE("missing glyphs and invalid UTF-8 draw the fallback glyph and are counted")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.text(face(), {0.0f, 0.0f},
                      "A\xE0\xB8\x81"
                      "B",
                      {}));
    CHECK(list.data().vertices.size() == 3 * 4);
    CHECK(list.missingGlyphs() == 1);
    const FontGlyph& question = test::testFont().glyphOrFallback(U'?');
    CHECK(quadAt(list.data(), 1).uvMin == question.uvMin);
    REQUIRE(list.text(face(), {0.0f, 0.0f}, "\xFF\xFE", {}));
    CHECK(list.missingGlyphs() == 3);
    CHECK(list.data().vertices.size() == 5 * 4);
    list.begin(hdCanvas());
    CHECK(list.missingGlyphs() == 0);
}

TEST_CASE("measuring counts invalid UTF-8 as replacement characters")
{
    UiTextStyle style;
    CHECK(measureText(test::testFont(), "\xFF", style).size.x ==
          measureText(test::testFont(), "?", style).size.x);
    CHECK(measureText(test::testFont(), "\xC3", style).lines == 1);
}

TEST_CASE("glyphs outside the clip are skipped, not dropped")
{
    UiDrawList list;
    list.begin(hdCanvas());
    REQUIRE(list.pushClip({{0.0f, 0.0f}, {50.0f, 50.0f}}));
    REQUIRE(list.text(face(), {500.0f, 500.0f}, "Hello", {}));
    CHECK(list.data().vertices.empty());
    REQUIRE(list.text(face(), {0.0f, 0.0f}, "Hello world, far beyond the clip", {}));
    CHECK_FALSE(list.data().vertices.empty());
    CHECK(list.data().vertices.size() < 26 * 4);
    CHECK(list.droppedPrimitives() == 0);
}

TEST_CASE("text breaks at line breaks")
{
    const Font& font = test::testFont();
    UiTextStyle style;
    style.size = 20.0f;
    const f32 lineStep = font.metrics().lineHeight() * 20.0f;
    CHECK(measureText(font, "", style).lines == 0);
    CHECK(measureText(font, "", style).size == Vec2(0.0f));
    const UiTextExtent one = measureText(font, "Hello", style);
    CHECK(one.lines == 1);
    CHECK(near(one.size.y, lineStep));
    const UiTextExtent two = measureText(font, "Hello\nwide world", style);
    CHECK(two.lines == 2);
    CHECK(near(two.size.x, measureText(font, "wide world", style).size.x));
    CHECK(near(two.size.y, 2.0f * lineStep));
    CHECK(measureText(font, "a\n", style).lines == 2);
    CHECK(measureText(font, "\n\n", style).lines == 3);
    style.lineSpacing = 1.5f;
    CHECK(near(measureText(font, "a\nb", style).size.y, 3.0f * lineStep, 1e-3f));
}

TEST_CASE("text wraps at spaces and splits words longer than a line")
{
    const Font& font = test::testFont();
    UiTextStyle style;
    style.size = 32.0f;
    const f32 word = textWidth("aaa");
    style.wrapWidth = word + 1.0f;
    const UiTextExtent wrapped = measureText(font, "aaa aaa  aaa", style);
    CHECK(wrapped.lines == 3);
    CHECK(near(wrapped.size.x, word));
    const UiTextExtent split = measureText(font, "aaaaaaaa", style);
    CHECK(split.lines == 3);
    CHECK(near(split.size.x, word));
    style.wrapWidth = 1.0f; // narrower than one glyph: still one glyph per line
    CHECK(measureText(font, "abc", style).lines == 3);
    style.wrapWidth = 0.0f;
    CHECK(measureText(font, "aaa aaa  aaa", style).lines == 1);

    // Drawn wrapped text has as many baselines as measured lines.
    UiDrawList list;
    list.begin(hdCanvas());
    style.wrapWidth = word + 1.0f;
    REQUIRE(list.text(face(), {0.0f, 0.0f}, "aaa aaa  aaa", style));
    REQUIRE(list.data().vertices.size() == 9 * 4);
    CHECK(quadAt(list.data(), 3).min.x == quadAt(list.data(), 0).min.x);
    CHECK(quadAt(list.data(), 3).min.y > quadAt(list.data(), 0).min.y);
    CHECK(quadAt(list.data(), 6).min.y > quadAt(list.data(), 3).min.y);
}

TEST_CASE("letter spacing and kerning change the advance between glyphs")
{
    const Font& font = test::testFont();
    UiTextStyle style;
    style.size = 32.0f;
    const FontGlyph& a = font.glyphOrFallback(U'A');
    const FontGlyph& v = font.glyphOrFallback(U'V');
    const f32 kerned = measureText(font, "AV", style).size.x;
    CHECK(near(kerned, (a.advance + font.kerning(U'A', U'V') + v.advance) * 32.0f));
    style.letterSpacing = 0.25f;
    CHECK(near(measureText(font, "AV", style).size.x, kerned + 8.0f));
    CHECK(near(measureText(font, "A", style).size.x, a.advance * 32.0f));
}

TEST_CASE("alignment places each line at, around or before the position")
{
    UiDrawList list;
    list.begin(hdCanvas());
    UiTransform2D unsnapped;
    unsnapped.translation = {0.5f, 0.0f};
    list.setTransform(unsnapped);
    UiTextStyle style;
    style.size = 32.0f;
    const f32 width = textWidth("Hello");
    REQUIRE(list.text(face(), {400.0f, 0.0f}, "Hello", style));
    style.align = UiTextAlign::Center;
    REQUIRE(list.text(face(), {400.0f, 0.0f}, "Hello", style));
    style.align = UiTextAlign::End;
    REQUIRE(list.text(face(), {400.0f, 0.0f}, "Hello", style));
    style.wrapWidth = 300.0f;
    REQUIRE(list.text(face(), {400.0f, 0.0f}, "Hello", style));
    style.align = UiTextAlign::Center;
    REQUIRE(list.text(face(), {400.0f, 0.0f}, "Hello", style));
    const UiDrawData data = list.data();
    const f32 start = quadAt(data, 0).min.x;
    CHECK(near(quadAt(data, 5).min.x, start - width * 0.5f, 1e-3f));
    CHECK(near(quadAt(data, 10).min.x, start - width, 1e-3f));
    CHECK(near(quadAt(data, 15).min.x, start + 300.0f - width, 1e-3f));
    CHECK(near(quadAt(data, 20).min.x, start + (300.0f - width) * 0.5f, 1e-3f));
}

TEST_CASE("the measured size matches where the glyphs are drawn")
{
    const Font& font = test::testFont();
    UiDrawList list;
    list.begin(hdCanvas());
    UiTransform2D unsnapped;
    unsnapped.translation = {0.25f, 0.0f};
    list.setTransform(unsnapped);
    UiTextStyle style;
    style.size = 27.0f;
    style.wrapWidth = 200.0f;
    style.letterSpacing = 0.05f;
    const std::string_view text = "Typography wraps, kerns and spaces";
    const UiTextExtent extent = measureText(font, text, style);
    REQUIRE(extent.lines > 1);
    REQUIRE(list.text(face(), {0.0f, 0.0f}, text, style));
    const UiDrawData data = list.data();
    // Right end of every line: the last glyph's origin plus its advance never passes the measured width,
    // and the widest line reaches it.
    f32 widest = 0.0f;
    f32 lastBaseline = 0.0f;
    const usize quads = data.vertices.size() / 4;
    usize quad = 0;
    for (const char c : text)
    {
        const FontGlyph& glyph = font.glyphOrFallback(static_cast<char32_t>(c));
        if (glyph.planeMin == glyph.planeMax)
        {
            continue;
        }
        REQUIRE(quad < quads);
        const Vec2 origin = quadAt(data, quad++).min - Vec2(0.25f, 0.0f) - glyph.planeMin * style.size;
        widest = std::max(widest, origin.x + glyph.advance * style.size);
        lastBaseline = origin.y;
    }
    CHECK(quad == quads);
    CHECK(near(widest, extent.size.x, 1e-3f));
    const f32 lineStep = font.metrics().lineHeight() * style.size;
    CHECK(near(lastBaseline,
               font.metrics().ascent * style.size + static_cast<f32>(extent.lines - 1) * lineStep, 1e-3f));
    CHECK(near(extent.size.y, static_cast<f32>(extent.lines) * lineStep, 1e-3f));
}

TEST_CASE("measuring with an invalid style or an empty font gives nothing")
{
    UiTextStyle style;
    style.size = -1.0f;
    CHECK(measureText(test::testFont(), "Hello", style).lines == 0);
    style = {};
    style.wrapWidth = kNaN;
    CHECK(measureText(test::testFont(), "Hello", style).lines == 0);
    const Font empty;
    CHECK(measureText(empty, "Hello", {}).lines == 0);
}

TEST_CASE("measuring text does not allocate")
{
    const Font& font = test::testFont();
    UiTextStyle style;
    style.wrapWidth = 120.0f;
    f32 width = 0.0f;
    const test::ScopedAllocationCounter counter;
    width += measureText(font, "A paragraph that wraps over several lines\nand breaks", style).size.x;
    width += measureText(font, "\xFF\xFE invalid", style).size.x;
    const usize allocations = counter.count();
    CHECK(allocations == 0);
    CHECK(width > 0.0f);
}

TEST_CASE("recording a frame does not allocate")
{
    const Font& font = test::testFont();
    UiDrawList list;
    const UiCanvasMetrics canvas = hdCanvas();
    UiTextStyle style;
    style.wrapWidth = 300.0f;
    style.align = UiTextAlign::Center;
    list.begin(canvas);
    REQUIRE(list.text(face(), {0.0f, 0.0f}, "warm up", style));

    const UiFontFace textFace = face();
    bool recorded = true;
    u32 lines = 0;
    const test::ScopedAllocationCounter counter;
    for (int frame = 0; frame < 3; ++frame)
    {
        list.begin(canvas);
        recorded = list.rect({{0.0f, 0.0f}, {100.0f, 100.0f}}, {0.1f, 0.2f, 0.3f, 0.5f}) && recorded;
        recorded = list.pushClip({{10.0f, 10.0f}, {500.0f, 500.0f}}) && recorded;
        list.setTransform(makeUiTransform({50.0f, 50.0f}, 0.1f, {1.1f, 1.1f}));
        recorded = list.image({{0.0f, 0.0f}, {64.0f, 64.0f}}, kPanel) && recorded;
        recorded = list.nineSlice({{0.0f, 0.0f}, {200.0f, 80.0f}}, kPanel, {32.0f, 32.0f},
                                  {8.0f, 8.0f, 8.0f, 8.0f}) &&
                   recorded;
        list.setTransform({});
        recorded = list.text(textFace, {20.0f, 20.0f},
                             "Score 1200\nLives \xE2\x82\xAC 3, a line long enough to wrap", style) &&
                   recorded;
        list.popClip();
        lines += measureText(font, "Score 1200", style).lines;
    }
    const usize allocations = counter.count();
    CHECK(allocations == 0);
    CHECK(recorded);
    CHECK(lines == 3);
    CHECK(list.data().commands.size() >= 3);
    CHECK(list.missingGlyphs() == 1);
}
