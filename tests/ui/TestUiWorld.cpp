#include "FontTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/StringTable.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr UiTextureId kAtlas{3};
constexpr UiTextureId kPanel{5};
constexpr Vec2 kPanelSize{64.0f, 32.0f};
constexpr UVec2 kHd{1920, 1080};

// Fonts and images with fixed answers: the test font, and a 64x32 panel.
class FakeAssets final : public UiAssets
{
public:
    UiFontFace font(const AssetRef& font) override
    {
        return fontReady && font == fontRef ? UiFontFace{&test::testFont(), kAtlas} : UiFontFace{};
    }

    UiImageSource image(const AssetRef& image) override
    {
        return imageReady && image == imageRef ? UiImageSource{kPanel, kPanelSize} : UiImageSource{};
    }

    AssetRef fontRef{"assets:/fonts/test.ttf"};
    AssetRef imageRef{"assets:/ui/panel.png"};
    bool fontReady = true;
    bool imageReady = true;
};

bool close(f32 a, f32 b, f32 tolerance = 1e-3f)
{
    return std::abs(a - b) <= tolerance;
}

bool close(Vec2 a, Vec2 b, f32 tolerance = 1e-3f)
{
    return close(a.x, b.x, tolerance) && close(a.y, b.y, tolerance);
}

bool close(const UiRect& a, const UiRect& b, f32 tolerance = 1e-3f)
{
    return close(a.min, b.min, tolerance) && close(a.max, b.max, tolerance);
}

UiCanvas fullCanvas(i32 order = 0)
{
    UiCanvas canvas;
    canvas.useSafeArea = false;
    canvas.order = order;
    return canvas;
}

UiElement placed(Vec2 anchorMin, Vec2 anchorMax, Vec2 offsetMin, Vec2 offsetMax)
{
    UiElement element;
    element.placement = {anchorMin, anchorMax, offsetMin, offsetMax};
    return element;
}

UiElement sized(Vec2 size)
{
    UiElement element;
    element.size = size;
    return element;
}

template <class... Ts>
Entity child(World& world, Entity parent, Ts&&... components)
{
    const Entity entity = world.create(std::forward<Ts>(components)...);
    REQUIRE(setParent(world, entity, parent).hasValue());
    return entity;
}

UiRect rectOf(const World& world, Entity entity)
{
    const UiComputedRect* computed = world.tryGet<UiComputedRect>(entity);
    REQUIRE(computed != nullptr);
    return computed->rect;
}

UiImage solid(Vec4 tint)
{
    UiImage image;
    image.tint = tint;
    return image;
}

UiText textOf(const FakeAssets& assets, std::string_view literal)
{
    UiText text;
    text.text = std::string(literal);
    text.font = assets.fontRef;
    return text;
}

usize quadCount(const UiDrawData& data)
{
    return data.vertices.size() / 4;
}

Vec2 measured(std::string_view utf8, f32 wrapWidth, f32 size = 24.0f)
{
    UiTextStyle style;
    style.size = size;
    style.wrapWidth = wrapWidth;
    return measureText(test::testFont(), utf8, style).size;
}

// Brings Inactive tags up to date, then updates the UI.
void step(World& world, HierarchyIndex& index, UiWorld& ui, const UiFrame& frame = {kHd})
{
    updateTransformHierarchy(world, index);
    ui.update(frame);
}

} // namespace

TEST_CASE("anchors and offsets place elements on targets of any size")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    const Entity corner =
        child(world, canvas, placed({1.0f, 0.0f}, {1.0f, 0.0f}, {-320.0f, 24.0f}, {-24.0f, 88.0f}));
    const Entity fill =
        child(world, canvas, placed({0.0f, 0.0f}, {1.0f, 1.0f}, {10.0f, 10.0f}, {-10.0f, -10.0f}));

    step(world, index, ui, {kHd});
    CHECK(ui.statistics().canvases == 1);
    CHECK(ui.statistics().elements == 2);
    CHECK(close(rectOf(world, corner), {{1600.0f, 24.0f}, {1896.0f, 88.0f}}));
    CHECK(close(rectOf(world, fill), {{10.0f, 10.0f}, {1910.0f, 1070.0f}}));
    CHECK(close(ui.drawData().canvas.pixelsPerUnit, 1.0f));

    // A 5:4 target fits the reference width: the canvas is taller, the pixels per unit fewer.
    step(world, index, ui, {{1280, 1024}});
    CHECK(close(ui.drawData().canvas.pixelsPerUnit, 1280.0f / 1920.0f));
    CHECK(close(rectOf(world, corner), {{1600.0f, 24.0f}, {1896.0f, 88.0f}}));
    CHECK(close(rectOf(world, fill), {{10.0f, 10.0f}, {1910.0f, 1526.0f}}));
}

TEST_CASE("canvases that use the safe area lay out inside the display's insets")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    UiCanvas safe;
    safe.useSafeArea = true;
    const Entity inside = world.create(safe);
    const Entity whole = world.create(fullCanvas(1));
    const UiElement fillElement = placed({0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f});
    const Entity insideFill = child(world, inside, fillElement);
    const Entity wholeFill = child(world, whole, fillElement);
    UiFrame frame{kHd, {}};
    frame.safeInsetPixels = {100.0f, 20.0f, 50.0f, 10.0f};
    step(world, index, ui, frame);
    CHECK(close(rectOf(world, insideFill), {{100.0f, 20.0f}, {1870.0f, 1070.0f}}));
    CHECK(close(rectOf(world, wholeFill), {{0.0f, 0.0f}, {1920.0f, 1080.0f}}));

    world.get<UiCanvas>(inside).useSafeArea = false;
    world.markChanged<UiCanvas>();
    step(world, index, ui, frame);
    CHECK(close(rectOf(world, insideFill), {{0.0f, 0.0f}, {1920.0f, 1080.0f}}));
}

TEST_CASE("a vertical stack places fixed, content and stretched children with every cross alignment")
{
    constexpr std::array kAligns{UiCrossAlign::Start, UiCrossAlign::Center, UiCrossAlign::End,
                                 UiCrossAlign::Stretch};
    for (const UiCrossAlign align : kAligns)
    {
        CAPTURE(static_cast<int>(align));
        World world;
        HierarchyIndex index;
        FakeAssets assets;
        UiWorld ui(world);
        ui.setAssets(&assets);
        const Entity canvas = world.create(fullCanvas());
        UiStack stack;
        stack.axis = UiAxis::Vertical;
        stack.spacing = 10.0f;
        stack.padding = {5.0f, 5.0f, 5.0f, 5.0f};
        stack.crossAlign = align;
        const Entity column =
            child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {400.0f, 600.0f}), stack);
        const Entity fixed = child(world, column, sized({100.0f, 50.0f}));
        UiImage panel;
        panel.image = assets.imageRef;
        const Entity image = child(world, column, UiElement{}, panel);
        const Entity label = child(world, column, UiElement{}, textOf(assets, "Hello"));
        const Entity first = child(world, column, UiElement{});
        const Entity second = child(world, column, UiElement{});
        step(world, index, ui);

        const Vec2 text = measured("Hello", 390.0f);
        const f32 share = (590.0f - 50.0f - kPanelSize.y - text.y - 40.0f) * 0.5f;
        const f32 widths[] = {100.0f, kPanelSize.x, text.x, 390.0f, 390.0f};
        const f32 heights[] = {50.0f, kPanelSize.y, text.y, share, share};
        const Entity children[] = {fixed, image, label, first, second};
        f32 y = 5.0f;
        for (usize i = 0; i < 5; ++i)
        {
            CAPTURE(i);
            const f32 width = align == UiCrossAlign::Stretch ? 390.0f : widths[i];
            const f32 x = align == UiCrossAlign::Center ? 5.0f + (390.0f - width) * 0.5f
                          : align == UiCrossAlign::End  ? 395.0f - width
                                                        : 5.0f;
            CHECK(close(rectOf(world, children[i]), {{x, y}, {x + width, y + heights[i]}}));
            y += heights[i] + 10.0f;
        }
    }
}

TEST_CASE("a horizontal stack shares the rest of its width among children without a size")
{
    World world;
    HierarchyIndex index;
    FakeAssets assets;
    UiWorld ui(world);
    ui.setAssets(&assets);
    const Entity canvas = world.create(fullCanvas());
    UiStack stack;
    stack.axis = UiAxis::Horizontal;
    stack.spacing = 8.0f;
    stack.padding = {10.0f, 4.0f, 10.0f, 4.0f};
    stack.crossAlign = UiCrossAlign::End;
    const Entity row =
        child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f}, {900.0f, 200.0f}), stack);
    const Entity fixed = child(world, row, sized({120.0f, 40.0f}));
    const Entity label = child(world, row, UiElement{}, textOf(assets, "Score"));
    const Entity grow = child(world, row, UiElement{});
    step(world, index, ui);

    const Vec2 text = measured("Score", 780.0f);
    const f32 share = 780.0f - 120.0f - text.x - 16.0f;
    CHECK(close(rectOf(world, fixed), {{110.0f, 156.0f}, {230.0f, 196.0f}}));
    CHECK(close(rectOf(world, label), {{238.0f, 196.0f - text.y}, {238.0f + text.x, 196.0f}}));
    CHECK(close(rectOf(world, grow), {{246.0f + text.x, 104.0f}, {246.0f + text.x + share, 196.0f}}));
}

TEST_CASE("a grid places children in rows of cells")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    UiGrid grid;
    grid.columns = 3;
    grid.cellSize = {100.0f, 50.0f};
    grid.spacing = {10.0f, 20.0f};
    grid.padding = {5.0f, 7.0f, 0.0f, 0.0f};
    const Entity table =
        child(world, canvas, placed({0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}), grid);
    std::vector<Entity> cells;
    for (int i = 0; i < 4; ++i)
    {
        cells.push_back(child(world, table, UiElement{}));
    }
    const Entity narrow = child(world, table, sized({50.0f, 0.0f}));
    step(world, index, ui);
    CHECK(close(rectOf(world, cells[0]), {{5.0f, 7.0f}, {105.0f, 57.0f}}));
    CHECK(close(rectOf(world, cells[2]), {{225.0f, 7.0f}, {325.0f, 57.0f}}));
    CHECK(close(rectOf(world, cells[3]), {{5.0f, 77.0f}, {105.0f, 127.0f}}));
    CHECK(close(rectOf(world, narrow), {{140.0f, 77.0f}, {190.0f, 127.0f}}));
}

TEST_CASE("rotation, scale and opacity accumulate down the tree without moving the layout")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    UiElement outer = placed({0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f}, {300.0f, 200.0f});
    outer.rotation = std::numbers::pi_v<f32> * 0.5f;
    outer.scale = {2.0f, 2.0f};
    outer.opacity = 0.5f;
    const Entity parent = child(world, canvas, outer, solid(Vec4(1.0f)));
    UiElement inner = placed({0.0f, 0.0f}, {1.0f, 1.0f}, {10.0f, 10.0f}, {-10.0f, -10.0f});
    inner.pivot = {0.0f, 0.0f};
    inner.scale = {0.5f, 0.5f};
    inner.opacity = 0.5f;
    const Entity nested = child(world, parent, inner, solid(Vec4(1.0f)));
    step(world, index, ui);

    const UiComputedRect& top = world.get<UiComputedRect>(parent);
    const UiComputedRect& below = world.get<UiComputedRect>(nested);
    CHECK(close(below.rect, {{110.0f, 110.0f}, {290.0f, 190.0f}}));
    CHECK(close(top.opacity, 0.5f));
    CHECK(close(below.opacity, 0.25f));
    const UiTransform2D expectedTop = makeUiTransform({200.0f, 150.0f}, outer.rotation, outer.scale);
    const UiTransform2D expectedBelow =
        combine(expectedTop, makeUiTransform({110.0f, 110.0f}, 0.0f, inner.scale));
    CHECK(close(top.transform.x, expectedTop.x));
    CHECK(close(top.transform.translation, expectedTop.translation));
    CHECK(close(below.transform.x, expectedBelow.x));
    CHECK(close(below.transform.y, expectedBelow.y));
    CHECK(close(below.transform.translation, expectedBelow.translation));

    const UiDrawData data = ui.drawData();
    REQUIRE(quadCount(data) == 2);
    CHECK(close(data.vertices[4].position, expectedBelow.apply({110.0f, 110.0f}), 1e-2f));
    CHECK((data.vertices[0].color >> 24) == 128u);
    CHECK((data.vertices[4].color >> 24) == 64u);
}

TEST_CASE("clipChildren clips the descendants of an element, axis-aligned even under a rotation")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    UiElement frame = placed({0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f}, {300.0f, 200.0f});
    frame.clipChildren = true;
    const Entity clipper = child(world, canvas, frame, solid(Vec4(1.0f)));
    (void)child(world, clipper, placed({0.0f, 0.0f}, {1.0f, 1.0f}, {-50.0f, -50.0f}, {50.0f, 50.0f}),
                solid(Vec4(1.0f, 0.0f, 0.0f, 1.0f)));
    (void)child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {10.0f, 10.0f}),
                solid(Vec4(0.0f, 1.0f, 0.0f, 1.0f)));
    step(world, index, ui);

    const UiRect whole{{0.0f, 0.0f}, {1920.0f, 1080.0f}};
    UiDrawData data = ui.drawData();
    REQUIRE(data.commands.size() == 3);
    CHECK(close(data.commands[0].clip, whole));
    CHECK(close(data.commands[1].clip, {{100.0f, 100.0f}, {300.0f, 200.0f}}));
    CHECK(close(data.commands[2].clip, whole));

    world.get<UiElement>(clipper).rotation = 0.3f;
    world.markChanged<UiElement>();
    step(world, index, ui);
    data = ui.drawData();
    REQUIRE(data.commands.size() == 3);
    CHECK(close(data.commands[1].clip, {{100.0f, 100.0f}, {300.0f, 200.0f}}));

    world.get<UiElement>(clipper).rotation = 0.0f;
    world.get<UiElement>(clipper).scale = {2.0f, 2.0f};
    world.markChanged<UiElement>();
    step(world, index, ui);
    data = ui.drawData();
    REQUIRE(data.commands.size() == 3);
    CHECK(close(data.commands[1].clip, {{0.0f, 50.0f}, {400.0f, 250.0f}}));
}

TEST_CASE("Inactive elements and canvases are skipped with their subtrees")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    const UiElement box = placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {10.0f, 10.0f});
    const Entity panel = child(world, canvas, box, solid(Vec4(1.0f)));
    (void)child(world, panel, box, solid(Vec4(1.0f)));
    (void)child(world, canvas, box, solid(Vec4(1.0f)));
    step(world, index, ui);
    CHECK(ui.statistics().elements == 3);
    CHECK(quadCount(ui.drawData()) == 3);

    setEnabled(world, panel, false);
    step(world, index, ui);
    CHECK(ui.statistics().elements == 1);
    CHECK(quadCount(ui.drawData()) == 1);

    setEnabled(world, canvas, false);
    step(world, index, ui);
    CHECK(ui.statistics().canvases == 0);
    CHECK(quadCount(ui.drawData()) == 0);
}

TEST_CASE("canvases are drawn by order, ties by Entity index")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const UiElement fill = placed({0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f});
    const Entity top = world.create(fullCanvas(2));
    (void)child(world, top, fill, solid(Vec4(1.0f, 0.0f, 0.0f, 1.0f)));
    const Entity bottom = world.create(fullCanvas(-1));
    (void)child(world, bottom, fill, solid(Vec4(0.0f, 1.0f, 0.0f, 1.0f)));
    const Entity middleA = world.create(fullCanvas(0));
    (void)child(world, middleA, fill, solid(Vec4(0.0f, 0.0f, 1.0f, 1.0f)));
    const Entity middleB = world.create(fullCanvas(0));
    (void)child(world, middleB, fill, solid(Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
    step(world, index, ui);
    const UiDrawData data = ui.drawData();
    REQUIRE(quadCount(data) == 4);
    // Red is in the lowest byte.
    CHECK((data.vertices[0].color & 0xFFFFFFu) == 0x00FF00u);
    CHECK((data.vertices[4].color & 0xFFFFFFu) == 0xFF0000u);
    CHECK((data.vertices[8].color & 0xFFFFFFu) == 0xFFFFFFu);
    CHECK((data.vertices[12].color & 0xFFFFFFu) == 0x0000FFu);
}

TEST_CASE("canvases with different scales share the draw list in the units of the first")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity first = world.create(fullCanvas(0));
    UiCanvas pixels = fullCanvas(1);
    pixels.scale.mode = UiScaleMode::Pixels;
    const Entity second = world.create(pixels);
    const UiElement box = placed({0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f}, {200.0f, 200.0f});
    (void)child(world, first, box, solid(Vec4(1.0f)));
    (void)child(world, second, box, solid(Vec4(1.0f)));
    step(world, index, ui, {{3840, 2160}});
    const UiDrawData data = ui.drawData();
    REQUIRE(quadCount(data) == 2);
    CHECK(close(data.canvas.pixelsPerUnit, 2.0f));
    CHECK(close(data.vertices[0].position, {100.0f, 100.0f}));
    CHECK(close(data.vertices[4].position, {50.0f, 50.0f}));
}

TEST_CASE("Entities without UiElement below a canvas are ignored with their subtrees")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    const Entity group = child(world, canvas, Name{"group"});
    const Entity lost = child(world, group, UiElement{}, solid(Vec4(1.0f)));
    const Entity kept = child(world, canvas, placed({0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}),
                              solid(Vec4(1.0f)));
    step(world, index, ui);
    CHECK(ui.statistics().elements == 1);
    CHECK(world.tryGet<UiComputedRect>(lost) == nullptr);
    CHECK(world.tryGet<UiComputedRect>(kept) != nullptr);
    CHECK(quadCount(ui.drawData()) == 1);
}

TEST_CASE("elements beyond maxElements or maxDepth are skipped and counted")
{
    SUBCASE("maxElements")
    {
        World world;
        HierarchyIndex index;
        UiWorldDesc desc;
        desc.maxElements = 2;
        UiWorld ui(world, desc);
        const Entity canvas = world.create(fullCanvas());
        for (int i = 0; i < 4; ++i)
        {
            (void)child(world, canvas, placed({0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}),
                        solid(Vec4(1.0f)));
        }
        step(world, index, ui);
        CHECK(ui.statistics().elements == 2);
        CHECK(ui.statistics().skippedElements == 2);
        CHECK(quadCount(ui.drawData()) == 2);
    }
    SUBCASE("maxDepth")
    {
        World world;
        HierarchyIndex index;
        UiWorldDesc desc;
        desc.maxDepth = 2;
        UiWorld ui(world, desc);
        Entity parent = world.create(fullCanvas());
        for (int i = 0; i < 4; ++i)
        {
            parent = child(world, parent, UiElement{}, solid(Vec4(1.0f)));
        }
        step(world, index, ui);
        CHECK(ui.statistics().elements == 2);
        CHECK(ui.statistics().skippedElements == 2);
    }
}

namespace
{

constexpr std::string_view kGameKeys[] = {"game.title"};
constexpr std::string_view kGameEnglish[] = {"Illusion"};
const text::StringTableData kGameTable{"game", kGameKeys, kGameEnglish};

} // namespace

TEST_CASE("UiText shows the message of its key, or the key itself when it is missing")
{
    World world;
    HierarchyIndex index;
    FakeAssets assets;
    text::Localization localization;
    localization.addTable(kGameTable);
    UiWorld ui(world);
    ui.setAssets(&assets);
    const Entity canvas = world.create(fullCanvas());
    UiText title = textOf(assets, "ignored");
    title.key = "game.title";
    UiText missing = textOf(assets, "");
    missing.key = "game.missing";
    (void)child(world, canvas, placed({0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 40.0f}), title);
    (void)child(world, canvas, placed({0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 50.0f}, {0.0f, 90.0f}), missing);

    step(world, index, ui);
    CHECK(ui.statistics().missingKeys == 2);
    CHECK(quadCount(ui.drawData()) == std::string_view("game.titlegame.missing").size());

    ui.setLocalization(&localization);
    step(world, index, ui);
    CHECK_FALSE(ui.statistics().layoutReused);
    CHECK(ui.statistics().missingKeys == 1);
    CHECK(quadCount(ui.drawData()) == std::string_view("Illusiongame.missing").size());
}

TEST_CASE("without assets only solid colours are drawn, and text without a font draws nothing")
{
    World world;
    HierarchyIndex index;
    FakeAssets assets;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    const UiElement box = placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {300.0f, 40.0f});
    UiImage panel;
    panel.image = assets.imageRef;
    (void)child(world, canvas, box, panel);
    (void)child(world, canvas, box, solid(Vec4(1.0f)));
    (void)child(world, canvas, box, textOf(assets, "Hello"));
    step(world, index, ui);
    CHECK(quadCount(ui.drawData()) == 1);

    ui.setAssets(&assets);
    step(world, index, ui);
    CHECK(quadCount(ui.drawData()) == 2 + 5);
    REQUIRE(ui.drawData().commands.size() >= 2);
    CHECK(ui.drawData().commands[0].texture == kPanel);
}

TEST_CASE("UiImage draws nine-slice panels and keeps the aspect when asked")
{
    World world;
    HierarchyIndex index;
    FakeAssets assets;
    UiWorld ui(world);
    ui.setAssets(&assets);
    const Entity canvas = world.create(fullCanvas());
    UiImage sliced;
    sliced.image = assets.imageRef;
    sliced.border = {8.0f, 8.0f, 8.0f, 8.0f};
    (void)child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {200.0f, 100.0f}), sliced);
    UiImage fitted;
    fitted.image = assets.imageRef;
    fitted.preserveAspect = true;
    (void)child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 200.0f}, {200.0f, 400.0f}), fitted);
    step(world, index, ui);
    const UiDrawData data = ui.drawData();
    REQUIRE(quadCount(data) == 10);
    CHECK(close(data.vertices[9 * 4].position, {0.0f, 250.0f}));
    CHECK(close(data.vertices[9 * 4 + 2].position, {200.0f, 350.0f}));
}

TEST_CASE("the layout is reused until something it depends on changes")
{
    World world;
    HierarchyIndex index;
    FakeAssets assets;
    FakeAssets otherAssets;
    text::Localization localization;
    localization.addTable(kGameTable);
    UiWorld ui(world);
    ui.setAssets(&assets);
    ui.setLocalization(&localization);
    const Entity canvas = world.create(fullCanvas());
    const Entity column = child(world, canvas, UiElement{}, UiStack{});
    const Entity label = child(world, column, UiElement{}, textOf(assets, "Hello"));
    const Entity table = child(world, canvas, UiElement{}, UiGrid{});
    (void)child(world, table, UiElement{}, solid(Vec4(1.0f)));

    UiFrame frame{kHd, {}};
    const auto expectRelayout = [&](const char* change) {
        CAPTURE(change);
        step(world, index, ui, frame);
        CHECK_FALSE(ui.statistics().layoutReused);
        step(world, index, ui, frame);
        CHECK(ui.statistics().layoutReused);
    };
    expectRelayout("first update");
    CHECK(quadCount(ui.drawData()) == 6);

    world.markChanged<UiCanvas>();
    expectRelayout("UiCanvas");
    world.markChanged<UiElement>();
    expectRelayout("UiElement");
    world.markChanged<UiImage>();
    expectRelayout("UiImage");
    world.markChanged<UiText>();
    expectRelayout("UiText");
    world.markChanged<UiStack>();
    expectRelayout("UiStack");
    world.markChanged<UiGrid>();
    expectRelayout("UiGrid");
    REQUIRE(reorder(world, table, 0).hasValue());
    expectRelayout("Hierarchy");
    setEnabled(world, label, false);
    expectRelayout("Inactive added");
    CHECK(quadCount(ui.drawData()) == 1);
    setEnabled(world, label, true);
    expectRelayout("Inactive removed");
    CHECK(quadCount(ui.drawData()) == 6);
    frame.targetPixels = {1280, 720};
    expectRelayout("target size");
    frame.safeInsetPixels.top = 4.0f;
    expectRelayout("safe insets");
    ui.setAssets(&otherAssets);
    expectRelayout("assets");
    localization.setLocale(*text::LocaleId::parse("tr"));
    localization.applyPendingLocale();
    expectRelayout("locale");

    // A font that is still loading lays out again every update until it is there.
    otherAssets.fontReady = false;
    world.markChanged<UiText>();
    step(world, index, ui, frame);
    step(world, index, ui, frame);
    CHECK_FALSE(ui.statistics().layoutReused);
    CHECK(quadCount(ui.drawData()) == 1);
    otherAssets.fontReady = true;
    expectRelayout("font loaded");
}

TEST_CASE("every UI Component survives a scene save and load")
{
    registerUiComponents();
    reflectCoreComponents();
    World world;
    UiCanvas canvas;
    canvas.scale = {UiScaleMode::MatchHeight, {1280.0f, 720.0f}, 1.5f};
    canvas.order = 3;
    canvas.useSafeArea = false;
    canvas.modal = true;
    UiElement element;
    element.placement = {{0.1f, 0.2f}, {0.9f, 0.8f}, {1.0f, 2.0f}, {-3.0f, -4.0f}};
    element.size = {10.0f, 20.0f};
    element.pivot = {0.25f, 0.75f};
    element.rotation = 0.5f;
    element.scale = {1.5f, 0.5f};
    element.opacity = 0.25f;
    element.clipChildren = true;
    UiImage image;
    image.image = AssetRef("assets:/ui/panel.png");
    image.tint = {0.1f, 0.2f, 0.3f, 0.4f};
    image.border = {1.0f, 2.0f, 3.0f, 4.0f};
    image.borderScale = 2.0f;
    image.preserveAspect = true;
    UiText text;
    text.text = "Hello";
    text.key = "game.title";
    text.font = AssetRef("assets:/fonts/test.ttf");
    text.size = 32.0f;
    text.color = {0.5f, 0.6f, 0.7f, 0.8f};
    text.align = UiTextAlign::End;
    text.verticalAlign = UiVerticalAlign::Bottom;
    text.wrap = false;
    text.lineSpacing = 1.25f;
    text.letterSpacing = 0.05f;
    UiStack stack;
    stack.axis = UiAxis::Horizontal;
    stack.spacing = 6.0f;
    stack.padding = {1.0f, 2.0f, 3.0f, 4.0f};
    stack.crossAlign = UiCrossAlign::Center;
    UiGrid grid;
    grid.columns = 4;
    grid.cellSize = {30.0f, 40.0f};
    grid.spacing = {5.0f, 6.0f};
    grid.padding = {7.0f, 8.0f, 9.0f, 10.0f};
    (void)world.create(canvas);
    (void)world.create(element, image, text);
    (void)world.create(UiElement{}, stack);
    (void)world.create(UiElement{}, grid, UiComputedRect{});
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json.find("illusion.UiComputedRect") == std::string::npos);

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    u32 found = 0;
    loaded.query<const UiCanvas>().each([&](Entity, const UiCanvas& c) {
        ++found;
        CHECK(c.scale == canvas.scale);
        CHECK(c.order == 3);
        CHECK_FALSE(c.useSafeArea);
        CHECK(c.modal);
    });
    loaded.query<const UiElement, const UiImage, const UiText>().each(
        [&](Entity, const UiElement& e, const UiImage& i, const UiText& t) {
            ++found;
            CHECK(e.placement == element.placement);
            CHECK(e.size == element.size);
            CHECK(e.pivot == element.pivot);
            CHECK(close(e.rotation, element.rotation));
            CHECK(e.scale == element.scale);
            CHECK(e.opacity == element.opacity);
            CHECK(e.clipChildren);
            CHECK(i.image == image.image);
            CHECK(i.tint == image.tint);
            CHECK(i.border == image.border);
            CHECK(i.borderScale == image.borderScale);
            CHECK(i.preserveAspect);
            CHECK(t.text == text.text);
            CHECK(t.key == text.key);
            CHECK(t.font == text.font);
            CHECK(t.size == text.size);
            CHECK(t.color == text.color);
            CHECK(t.align == text.align);
            CHECK(t.verticalAlign == text.verticalAlign);
            CHECK_FALSE(t.wrap);
            CHECK(t.lineSpacing == text.lineSpacing);
            CHECK(t.letterSpacing == text.letterSpacing);
        });
    loaded.query<const UiStack>().each([&](Entity, const UiStack& s) {
        ++found;
        CHECK(s.axis == stack.axis);
        CHECK(s.spacing == stack.spacing);
        CHECK(s.padding == stack.padding);
        CHECK(s.crossAlign == stack.crossAlign);
    });
    loaded.query<const UiGrid>().each([&](Entity, const UiGrid& g) {
        ++found;
        CHECK(g.columns == grid.columns);
        CHECK(g.cellSize == grid.cellSize);
        CHECK(g.spacing == grid.spacing);
        CHECK(g.padding == grid.padding);
    });
    CHECK(found == 4);
}

TEST_CASE("a steady UiWorld update allocates nothing")
{
    World world;
    HierarchyIndex index;
    FakeAssets assets;
    text::Localization localization;
    localization.addTable(kGameTable);
    UiWorld ui(world);
    ui.setAssets(&assets);
    ui.setLocalization(&localization);
    const Entity canvas = world.create(fullCanvas());
    UiStack stack;
    stack.crossAlign = UiCrossAlign::Center;
    UiElement clipped = placed({0.0f, 0.0f}, {0.5f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f});
    clipped.clipChildren = true;
    clipped.rotation = 0.1f;
    const Entity column = child(world, canvas, clipped, stack);
    UiText title = textOf(assets, "");
    title.key = "game.title";
    title.verticalAlign = UiVerticalAlign::Middle;
    (void)child(world, column, UiElement{}, title);
    (void)child(world, column, UiElement{}, textOf(assets, "Score 1200"));
    UiImage panel;
    panel.image = assets.imageRef;
    panel.border = {4.0f, 4.0f, 4.0f, 4.0f};
    (void)child(world, column, sized({0.0f, 40.0f}), panel);
    const Entity table =
        child(world, canvas, placed({0.5f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}), UiGrid{});
    for (int i = 0; i < 6; ++i)
    {
        (void)child(world, table, UiElement{}, solid(Vec4(0.5f)));
    }
    step(world, index, ui);
    step(world, index, ui);
    REQUIRE(ui.statistics().layoutReused);
    updateTransformHierarchy(world, index);
    {
        const test::ScopedAllocationCounter counter;
        ui.update({kHd});
        CHECK(counter.count() == 0);
    }
    CHECK(ui.statistics().layoutReused);

    // Laying out again without structural changes does not allocate either.
    world.markChanged<UiText>();
    {
        const test::ScopedAllocationCounter counter;
        ui.update({kHd});
        CHECK(counter.count() == 0);
    }
    CHECK_FALSE(ui.statistics().layoutReused);
    CHECK(quadCount(ui.drawData()) > 9);
}

TEST_CASE("layout() lists the canvases and elements of the last update in draw order")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    UiLayout layout;
    ui.layout(layout);
    CHECK(layout.canvases.empty());

    const Entity top = world.create(fullCanvas(1));
    const Entity bottom = world.create(fullCanvas(0));
    const Entity panel =
        child(world, bottom, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {10.0f, 20.0f}, {110.0f, 70.0f}));
    UiElement faded = placed({0.0f, 0.0f}, {1.0f, 1.0f}, {}, {});
    faded.opacity = 0.0f;
    faded.clipChildren = true;
    const Entity hidden = child(world, panel, faded);
    const Entity label = child(world, top, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {50.0f, 50.0f}));
    step(world, index, ui, {{960, 540}});

    ui.layout(layout);
    REQUIRE(layout.canvases.size() == 2);
    CHECK(layout.canvases[0].entity == bottom);
    CHECK(layout.canvases[1].entity == top);
    CHECK(close(layout.canvases[0].metrics.pixelsPerUnit, 0.5f));
    CHECK(close(layout.canvases[0].area, {{0.0f, 0.0f}, {1920.0f, 1080.0f}}));
    CHECK(layout.canvases[0].pointerEligible);
    REQUIRE(layout.elements.size() == 3);
    CHECK(layout.elements[0].entity == panel);
    CHECK(layout.elements[0].canvas == 0);
    CHECK(layout.elements[0].parent == UiLayoutElement::kNoParent);
    CHECK(close(layout.elements[0].rect, {{10.0f, 20.0f}, {110.0f, 70.0f}}));
    CHECK(layout.elements[0].visible);
    CHECK(layout.elements[1].entity == hidden);
    CHECK(layout.elements[1].parent == 0);
    CHECK_FALSE(layout.elements[1].visible);
    CHECK(layout.elements[1].clipsChildren);
    CHECK(layout.elements[2].entity == label);
    CHECK(layout.elements[2].canvas == 1);

    // Entities destroyed after the update are left out.
    REQUIRE(destroyRecursive(world, hidden) == 1);
    ui.layout(layout);
    CHECK(layout.elements.size() == 2);
}

TEST_CASE("hitTest() finds the topmost enabled interactable element of the last layout")
{
    World world;
    HierarchyIndex index;
    UiWorld ui(world);
    const Entity canvas = world.create(fullCanvas());
    const Entity under =
        child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {200.0f, 100.0f}),
              UiInteractable{}, UiButton{});
    const Entity over =
        child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 0.0f}, {300.0f, 100.0f}),
              UiInteractable{}, UiButton{});
    step(world, index, ui);
    CHECK(ui.hitTest({50.0f, 50.0f}) == under);
    CHECK(ui.hitTest({150.0f, 50.0f}) == over);
    CHECK(ui.hitTest({500.0f, 500.0f}).isNull());
    // A destroyed element is not hit before the next update lays the UI out again.
    REQUIRE(destroyRecursive(world, over) == 1);
    CHECK(ui.hitTest({150.0f, 50.0f}) == under);
}

TEST_CASE("a preview UiWorld clamps a scroll view's offset for its layout without writing it")
{
    World world;
    HierarchyIndex index;
    UiWorldDesc desc;
    desc.preview = true;
    UiWorld preview(world, desc);
    const Entity canvas = world.create(fullCanvas());
    UiScrollView scroll;
    scroll.offset = {0.0f, 5000.0f};
    const Entity view =
        child(world, canvas, placed({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f}), scroll);
    const Entity content = child(world, view, sized({100.0f, 300.0f}));
    step(world, index, preview);
    CHECK(world.get<UiScrollView>(view).offset.y == 5000.0f);
    // The content is scrolled by the largest offset its size allows.
    CHECK(close(rectOf(world, content), {{0.0f, -200.0f}, {100.0f, 100.0f}}));

    UiWorld game(world);
    step(world, index, game);
    CHECK(world.get<UiScrollView>(view).offset.y == 200.0f);
}
