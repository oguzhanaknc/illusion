// UiTransition in a UiWorld: triggers, delays, eases, accumulation to descendants, the cost while settled,
// and the outline and drop shadow of UiText.

#include "FontTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr UVec2 kHd{1920, 1080}; // one canvas unit per pixel
constexpr UiTextureId kAtlas{3};
constexpr f32 kNaN = std::numeric_limits<f32>::quiet_NaN();

class FontAssets final : public UiAssets
{
public:
    UiFontFace font(const AssetRef&) override { return {&test::testFont(), kAtlas}; }
    UiImageSource image(const AssetRef&) override { return {}; }
};

UiElement placed(Vec2 min, Vec2 max)
{
    UiElement element;
    element.placement.offsetMin = min;
    element.placement.offsetMax = max;
    return element;
}

UiImage solid(Vec4 tint = Vec4(1.0f))
{
    UiImage image;
    image.tint = tint;
    return image;
}

UiTransition transitionOf(UiTransitionTrigger trigger, f32 duration, UiEase ease = UiEase::Linear)
{
    UiTransition transition;
    transition.trigger = trigger;
    transition.duration = duration;
    transition.ease = ease;
    return transition;
}

UiInput elapsed(f64 seconds)
{
    UiInput input;
    input.deltaSeconds = seconds;
    return input;
}

UiInput pointerAt(Vec2 pixels, f64 seconds, bool down = false)
{
    UiInput input = elapsed(seconds);
    input.pointerPresent = true;
    input.pointerPixels = pixels;
    input.pointerDown = down;
    return input;
}

bool close(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}

u32 alphaOf(const UiVertex& vertex)
{
    return vertex.color >> 24u;
}

// A full-screen canvas of a World and its UiWorld.
struct Scene
{
    Scene()
        : ui(world)
    {
        UiCanvas settings;
        settings.useSafeArea = false;
        canvas = world.create(settings);
        ui.setAssets(&assets);
    }

    template <class... Ts>
    Entity child(Entity parent, Ts&&... components)
    {
        const Entity entity = world.create(std::forward<Ts>(components)...);
        REQUIRE(setParent(world, entity, parent).hasValue());
        return entity;
    }

    void step(const UiInput& input = {})
    {
        updateTransformHierarchy(world, index);
        ui.update({kHd}, input);
    }

    [[nodiscard]] f32 progress(Entity entity) const
    {
        const UiTransitionState* state = world.tryGet<UiTransitionState>(entity);
        REQUIRE(state != nullptr);
        return state->progress;
    }

    void play(Entity entity, bool towardsChanged)
    {
        UiTransitionState& state = world.get<UiTransitionState>(entity);
        state.towardsChanged = towardsChanged;
        state.waited = 0.0f;
        world.markChanged<UiTransitionState>();
    }

    // Top left corner of the quad @p quad of the last update.
    [[nodiscard]] Vec2 corner(usize quad) const
    {
        const UiDrawData data = ui.drawData();
        REQUIRE(data.vertices.size() >= (quad + 1) * 4);
        return data.vertices[quad * 4].position;
    }

    World world;
    HierarchyIndex index;
    FontAssets assets;
    UiWorld ui;
    Entity canvas{};
};

} // namespace

TEST_CASE("a Manual transition moves only when played, and every ease meets its curve at 0, 0.5 and 1")
{
    struct Curve
    {
        UiEase ease;
        f32 half;
    };
    // BackOut: 1 + 2.70158 * (-0.5)^3 + 1.70158 * (-0.5)^2, above 1 half way.
    constexpr std::array<Curve, 5> kCurves{{{UiEase::Linear, 0.5f},
                                            {UiEase::EaseIn, 0.125f},
                                            {UiEase::EaseOut, 0.875f},
                                            {UiEase::EaseInOut, 0.5f},
                                            {UiEase::BackOut, 1.0876975f}}};
    for (const Curve& curve : kCurves)
    {
        CAPTURE(static_cast<int>(curve.ease));
        Scene s;
        UiTransition transition = transitionOf(UiTransitionTrigger::Manual, 1.0f, curve.ease);
        transition.offset = {100.0f, 0.0f};
        const Entity panel =
            s.child(s.canvas, placed({100.0f, 100.0f}, {200.0f, 200.0f}), solid(), transition);
        s.step(elapsed(0.5));
        s.step(elapsed(0.5));
        CHECK(s.progress(panel) == 0.0f);
        CHECK(s.corner(0).x == 100.0f);

        s.play(panel, true);
        s.step(elapsed(0.5));
        CHECK(s.progress(panel) == 0.5f);
        CHECK(close(s.corner(0).x, 100.0f + 100.0f * curve.half));
        s.step(elapsed(0.5));
        CHECK(s.progress(panel) == 1.0f);
        CHECK(close(s.corner(0).x, 200.0f));
        s.step(elapsed(0.5));
        CHECK(close(s.corner(0).x, 200.0f));
        if (curve.ease == UiEase::BackOut)
        {
            CHECK(curve.half > 1.0f);
        }
    }
}

TEST_CASE("a Shown transition starts in the changed state, waits its delay, eases to rest and replays when "
          "shown again")
{
    Scene s;
    UiTransition transition = transitionOf(UiTransitionTrigger::Shown, 0.5f);
    transition.delay = 0.25f;
    transition.offset = {0.0f, 40.0f};
    const Entity panel = s.child(s.canvas, placed({100.0f, 100.0f}, {200.0f, 200.0f}), solid(), transition);

    // The update without input moves nothing and starts nothing.
    updateTransformHierarchy(s.world, s.index);
    s.ui.update({kHd});
    CHECK(s.progress(panel) == 0.0f);
    CHECK(s.corner(0).y == 100.0f);

    s.step(elapsed(0.125));
    CHECK(s.progress(panel) == 1.0f);
    CHECK(s.corner(0).y == 140.0f);
    s.step(elapsed(0.125));
    s.step(elapsed(0.125)); // the delay is spent
    CHECK(s.progress(panel) == 1.0f);
    s.step(elapsed(0.125));
    CHECK(s.progress(panel) == 0.75f);
    CHECK(close(s.corner(0).y, 130.0f));
    for (int i = 0; i < 4; ++i)
    {
        s.step(elapsed(0.125));
    }
    CHECK(s.progress(panel) == 0.0f);
    CHECK(s.corner(0).y == 100.0f);

    // Hidden through its parent and shown again: it plays again.
    const Entity group = s.child(s.canvas, placed({0.0f, 0.0f}, {1920.0f, 1080.0f}));
    REQUIRE(setParent(s.world, panel, group).hasValue());
    s.step(elapsed(0.125));
    CHECK(s.progress(panel) == 0.0f); // moving it did not hide it
    setEnabled(s.world, group, false);
    s.step(elapsed(0.125));
    CHECK_FALSE(s.world.get<UiTransitionState>(panel).started);
    setEnabled(s.world, group, true);
    s.step(elapsed(0.125));
    CHECK(s.progress(panel) == 1.0f);
    CHECK(s.corner(0).y == 140.0f);
}

TEST_CASE("Hovered, Pressed and Focused transitions follow the element's state, after their delay")
{
    Scene s;
    UiTransition hover = transitionOf(UiTransitionTrigger::Hovered, 0.5f);
    hover.delay = 0.25f;
    UiTransition press = transitionOf(UiTransitionTrigger::Pressed, 0.5f);
    UiTransition focus = transitionOf(UiTransitionTrigger::Focused, 0.5f);
    const Entity hovered = s.child(s.canvas, placed({0.0f, 0.0f}, {100.0f, 100.0f}), solid(),
                                   UiInteractable{}, UiButton{}, hover);
    const Entity pressed = s.child(s.canvas, placed({200.0f, 0.0f}, {300.0f, 100.0f}), solid(),
                                   UiInteractable{}, UiButton{}, press);
    const Entity focused = s.child(s.canvas, placed({400.0f, 0.0f}, {500.0f, 100.0f}), solid(),
                                   UiInteractable{}, UiButton{}, focus);
    // Without UiInteractable nothing holds a Hovered trigger.
    const Entity plain = s.child(s.canvas, placed({600.0f, 0.0f}, {700.0f, 100.0f}), solid(),
                                 transitionOf(UiTransitionTrigger::Hovered, 0.0f));
    s.step(elapsed(0.125));

    s.step(pointerAt({50.0f, 50.0f}, 0.125)); // hovered: the delay starts
    CHECK(s.progress(hovered) == 0.0f);
    s.step(pointerAt({50.0f, 50.0f}, 0.125));
    CHECK(s.progress(hovered) == 0.0f);
    s.step(pointerAt({50.0f, 50.0f}, 0.125));
    CHECK(s.progress(hovered) == 0.25f);
    s.step(pointerAt({650.0f, 50.0f}, 0.125)); // reversed half way, without a delay
    CHECK(s.progress(hovered) == 0.0f);
    CHECK(s.progress(plain) == 0.0f);

    s.step(pointerAt({250.0f, 50.0f}, 0.125, true));
    CHECK(s.progress(pressed) == 0.25f);
    s.step(pointerAt({250.0f, 50.0f}, 0.125, true));
    CHECK(s.progress(pressed) == 0.5f);
    s.step(pointerAt({250.0f, 50.0f}, 0.125, false));
    CHECK(s.progress(pressed) == 0.25f);

    s.ui.setFocus(focused);
    s.step(elapsed(0.25));
    CHECK(s.progress(focused) == 0.5f);
    s.step(elapsed(0.25));
    CHECK(s.progress(focused) == 1.0f);
    s.ui.setFocus({});
    s.step(elapsed(0.125));
    CHECK(s.progress(focused) == 0.75f);
}

TEST_CASE("a transition of zero duration switches at once, and invalid values count as zero or identity")
{
    Scene s;
    UiTransition instant = transitionOf(UiTransitionTrigger::Manual, 0.0f);
    instant.offset = {10.0f, 0.0f};
    UiTransition broken = transitionOf(UiTransitionTrigger::Manual, kNaN);
    broken.delay = -1.0f;
    broken.offset = {kNaN, 20.0f};
    broken.scale = {kNaN, kNaN};
    broken.opacity = kNaN;
    broken.tint = {-1.0f, kNaN, 1.0f, 1.0f};
    UiTransition negative = transitionOf(UiTransitionTrigger::Manual, -2.0f);
    const Entity a = s.child(s.canvas, placed({0.0f, 0.0f}, {100.0f, 100.0f}), solid(), instant);
    const Entity b = s.child(s.canvas, placed({200.0f, 0.0f}, {300.0f, 100.0f}), solid(), broken);
    const Entity c = s.child(s.canvas, placed({400.0f, 0.0f}, {500.0f, 100.0f}), solid(), negative);
    s.step(elapsed(0.0));
    s.play(a, true);
    s.play(b, true);
    s.play(c, true);
    s.step(elapsed(0.0));
    CHECK(s.progress(a) == 1.0f);
    CHECK(s.progress(b) == 1.0f);
    CHECK(s.progress(c) == 1.0f);
    CHECK(s.corner(0).x == 10.0f);
    // Only the finite part of the offset moves it; scale and opacity stay; the tint's red is clamped to 0
    // and its green, which is not finite, stays.
    CHECK(s.corner(1) == Vec2(200.0f, 20.0f));
    CHECK(s.ui.drawData().vertices[5].position == Vec2(300.0f, 20.0f));
    const u32 color = s.ui.drawData().vertices[4].color;
    CHECK((color & 0xFFu) == 0u);
    CHECK(((color >> 8u) & 0xFFu) == 255u);
    CHECK(alphaOf(s.ui.drawData().vertices[4]) == 255u);
    s.world.get<UiTransitionState>(b).progress = kNaN;
    s.world.markChanged<UiTransitionState>();
    s.step(elapsed(0.0));
    CHECK(s.progress(b) == 1.0f); // a progress that is not finite starts again from rest
}

TEST_CASE("a transition that scales an element to zero hits nothing and keeps the draw data finite")
{
    Scene s;
    UiTransition transition = transitionOf(UiTransitionTrigger::Manual, 0.0f);
    transition.scale = {0.0f, 0.0f};
    const Entity parent = s.child(s.canvas, placed({100.0f, 100.0f}, {300.0f, 300.0f}), solid(), transition);
    const Entity button =
        s.child(parent, placed({0.0f, 0.0f}, {100.0f, 100.0f}), solid(), UiInteractable{}, UiButton{});
    s.step(elapsed(0.0));
    CHECK(s.ui.hitTest({150.0f, 150.0f}) == button);
    s.play(parent, true);
    s.step(pointerAt({200.0f, 200.0f}, 0.0));
    CHECK(s.progress(parent) == 1.0f);
    CHECK(s.ui.hitTest({200.0f, 200.0f}).isNull());
    CHECK(s.ui.hitTest({150.0f, 150.0f}).isNull());
    for (const UiVertex& vertex : s.ui.drawData().vertices)
    {
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.y));
    }
}

TEST_CASE("a transition moves, scales, fades and tints its element's descendants, and hit tests follow")
{
    Scene s;
    UiTransition transition = transitionOf(UiTransitionTrigger::Manual, 0.0f);
    transition.offset = {50.0f, 0.0f};
    transition.scale = {2.0f, 2.0f};
    transition.opacity = 0.5f;
    transition.tint = {1.0f, 0.0f, 1.0f, 1.0f};
    const Entity parent = s.child(s.canvas, placed({100.0f, 100.0f}, {300.0f, 300.0f}), solid(), transition);
    const Entity button =
        s.child(parent, placed({0.0f, 0.0f}, {100.0f, 100.0f}), solid(), UiInteractable{}, UiButton{});
    UiText label;
    label.text = "A";
    label.size = 32.0f;
    (void)s.child(button, placed({0.0f, 0.0f}, {100.0f, 100.0f}), label);
    s.step(elapsed(0.0));
    const Vec2 restCorner = s.corner(2);
    const UiComputedRect rest = s.world.get<UiComputedRect>(button);
    CHECK(s.ui.hitTest({350.0f, 150.0f}).isNull());

    s.play(parent, true);
    s.step(elapsed(0.0));
    REQUIRE(s.ui.statistics().layoutReused);
    // Scaled by 2 about the parent's centre (200, 200), then moved by 50: the child's (100, 100) goes to
    // (50, 0); the canvas units are pixels.
    CHECK(close(s.corner(0).x, 50.0f));
    CHECK(close(s.corner(0).y, 0.0f));
    CHECK(close(s.corner(1).x, 50.0f));
    CHECK(close(s.corner(1).y, 0.0f));
    CHECK(close(s.ui.drawData().vertices[6].position.x, 250.0f));
    // Glyph origins snap to pixels only without a transform.
    CHECK(close(s.corner(2).x, 50.0f + (restCorner.x - 100.0f) * 2.0f, 1.0f));
    for (const UiVertex& vertex : s.ui.drawData().vertices)
    {
        CHECK(alphaOf(vertex) == 128u);
        CHECK(((vertex.color >> 8u) & 0xFFu) == 0u);
        CHECK((vertex.color & 0xFFu) == 255u);
    }
    CHECK(s.ui.hitTest({240.0f, 190.0f}) == button);
    CHECK(s.ui.hitTest({20.0f, 20.0f}).isNull());

    // The layout and the computed rectangles do not change.
    const UiComputedRect moved = s.world.get<UiComputedRect>(button);
    CHECK(moved.rect == rest.rect);
    CHECK(moved.transform == rest.transform);
    CHECK(moved.opacity == rest.opacity);
}

TEST_CASE("running transitions keep the layout, and settled ones write nothing and allocate nothing")
{
    Scene s;
    const Entity hover = s.child(s.canvas, placed({0.0f, 0.0f}, {100.0f, 100.0f}), solid(), UiInteractable{},
                                 UiButton{}, transitionOf(UiTransitionTrigger::Hovered, 0.5f));
    (void)s.child(s.canvas, placed({200.0f, 0.0f}, {300.0f, 100.0f}), solid(),
                  transitionOf(UiTransitionTrigger::Shown, 0.5f));
    s.step(pointerAt({50.0f, 50.0f}, 0.0));
    s.step(pointerAt({50.0f, 50.0f}, 0.125));
    REQUIRE(s.ui.statistics().layoutReused);
    CHECK(s.progress(hover) == 0.25f);
    updateTransformHierarchy(s.world, s.index);
    {
        const test::ScopedAllocationCounter counter;
        s.ui.update({kHd}, pointerAt({50.0f, 50.0f}, 0.125));
        CHECK(counter.count() == 0);
    }
    CHECK(s.ui.statistics().layoutReused);
    CHECK(s.progress(hover) == 0.5f);
    for (int i = 0; i < 4; ++i)
    {
        s.step(pointerAt({50.0f, 50.0f}, 0.125));
    }
    REQUIRE(s.progress(hover) == 1.0f);
    s.step(pointerAt({50.0f, 50.0f}, 0.125));
    const u64 version = s.world.componentVersion<UiTransitionState>();
    updateTransformHierarchy(s.world, s.index);
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 4; ++i)
        {
            s.ui.update({kHd}, pointerAt({50.0f, 50.0f}, 0.125));
        }
        CHECK(counter.count() == 0);
    }
    CHECK(s.ui.statistics().layoutReused);
    CHECK(s.world.componentVersion<UiTransitionState>() == version);
}

TEST_CASE("UiTransition survives a scene save and load; UiTransitionState is not saved")
{
    registerUiComponents();
    reflectCoreComponents();
    World world;
    UiTransition transition;
    transition.trigger = UiTransitionTrigger::Focused;
    transition.duration = 0.75f;
    transition.delay = 0.125f;
    transition.ease = UiEase::BackOut;
    transition.offset = {3.0f, -4.0f};
    transition.scale = {1.5f, 0.5f};
    transition.opacity = 0.25f;
    transition.tint = {0.1f, 0.2f, 0.3f, 0.4f};
    UiText text;
    text.outlineWidth = 2.5f;
    text.outlineColor = {0.1f, 0.2f, 0.3f, 0.9f};
    text.shadowOffset = {2.0f, 3.0f};
    text.shadowColor = {0.4f, 0.5f, 0.6f, 0.7f};
    (void)world.create(
        UiElement{}, transition, text,
        UiTransitionState{.progress = 0.5f, .waited = 0.0f, .towardsChanged = true, .started = true});
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json.find("illusion.UiTransitionState") == std::string::npos);

    World loaded;
    REQUIRE(loadScene(loaded, saved.json).ok());
    u32 found = 0;
    loaded.query<const UiTransition, const UiText>().each(
        [&](Entity entity, const UiTransition& t, const UiText& x) {
            ++found;
            CHECK(t.trigger == transition.trigger);
            CHECK(t.duration == transition.duration);
            CHECK(t.delay == transition.delay);
            CHECK(t.ease == transition.ease);
            CHECK(t.offset == transition.offset);
            CHECK(t.scale == transition.scale);
            CHECK(t.opacity == transition.opacity);
            CHECK(t.tint == transition.tint);
            CHECK(x.outlineWidth == text.outlineWidth);
            CHECK(x.outlineColor == text.outlineColor);
            CHECK(x.shadowOffset == text.shadowOffset);
            CHECK(x.shadowColor == text.shadowColor);
            CHECK_FALSE(loaded.has<UiTransitionState>(entity));
        });
    CHECK(found == 1);
}

TEST_CASE("a UiText with a shadow draws its quads at the offset in the shadow colour first, then the text")
{
    Scene s;
    UiText plain;
    plain.text = "Hi";
    plain.size = 32.0f;
    plain.color = {1.0f, 1.0f, 1.0f, 1.0f};
    const Entity text = s.child(s.canvas, placed({100.0f, 100.0f}, {600.0f, 200.0f}), plain);
    s.step(elapsed(0.0));
    const UiDrawData alone = s.ui.drawData();
    REQUIRE(alone.commands.size() == 1);
    const usize glyphs = alone.vertices.size() / 4;
    REQUIRE(glyphs == 2);
    const Vec2 first = alone.vertices[0].position;

    UiText& shadowed = s.world.get<UiText>(text);
    shadowed.shadowOffset = {4.0f, 6.0f};
    shadowed.shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    s.world.markChanged<UiText>();
    s.step(elapsed(0.0));
    const UiDrawData both = s.ui.drawData();
    REQUIRE(both.vertices.size() == glyphs * 8);
    CHECK(both.vertices[0].position == first + Vec2(4.0f, 6.0f));
    CHECK(alphaOf(both.vertices[0]) == 128u);
    CHECK((both.vertices[0].color & 0xFFFFFFu) == 0u);
    CHECK(both.vertices[glyphs * 4].position == first);
    CHECK(alphaOf(both.vertices[glyphs * 4]) == 255u);
    CHECK(both.commands.size() == 1); // same atlas and no outline: one batch

    // An outlined text: the shadow is its outline alone, in the shadow colour, with a transparent fill.
    shadowed.outlineWidth = 2.0f;
    shadowed.outlineColor = {1.0f, 0.0f, 0.0f, 1.0f};
    s.world.markChanged<UiText>();
    s.step(elapsed(0.0));
    const UiDrawData outlined = s.ui.drawData();
    REQUIRE(outlined.commands.size() == 2);
    CHECK(outlined.commands[0].sdfOutline == 2.0f); // 2 units at 32 per em, 32 texels per em
    CHECK(outlined.commands[0].outlineColor == 0x80000000u);
    CHECK(alphaOf(outlined.vertices[0]) == 0u);
    CHECK(outlined.commands[1].sdfOutline == 2.0f);
    CHECK(outlined.commands[1].outlineColor == 0xFF0000FFu);

    // Opacity fades the text, its outline and its shadow; the measured size does not change.
    s.world.get<UiElement>(text).opacity = 0.5f;
    s.world.markChanged<UiElement>();
    s.step(elapsed(0.0));
    CHECK(s.ui.drawData().commands[0].outlineColor == 0x40000000u);
    CHECK(s.ui.drawData().commands[1].outlineColor == 0x800000FFu);
    CHECK(alphaOf(s.ui.drawData().vertices[glyphs * 4]) == 128u);
    UiTextStyle style;
    style.size = 32.0f;
    const UiTextExtent bare = measureText(test::testFont(), "Hi", style);
    style.outlineWidth = 4.0f;
    CHECK(measureText(test::testFont(), "Hi", style).size == bare.size);
}
