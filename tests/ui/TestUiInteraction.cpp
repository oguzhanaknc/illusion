#include "TestSupport.hpp"

#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace illusion;

namespace
{

constexpr UVec2 kHd{1920, 1080};
constexpr f64 kFrame = 1.0 / 60.0;

UiCanvas fullCanvas(i32 order = 0, bool modal = false)
{
    UiCanvas canvas;
    canvas.useSafeArea = false;
    canvas.order = order;
    canvas.modal = modal;
    return canvas;
}

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

UiInput pointerAt(Vec2 pixels, bool down = false)
{
    UiInput input;
    input.pointerPixels = pixels;
    input.pointerPresent = true;
    input.pointerDown = down;
    input.deltaSeconds = kFrame;
    return input;
}

UiInput navigating(Vec2 direction, f64 deltaSeconds = kFrame)
{
    UiInput input;
    input.navigate = direction;
    input.deltaSeconds = deltaSeconds;
    return input;
}

UiInput submitting(bool down)
{
    UiInput input;
    input.submitDown = down;
    input.deltaSeconds = kFrame;
    return input;
}

constexpr Vec2 kUp{0.0f, 1.0f};
constexpr Vec2 kDown{0.0f, -1.0f};
constexpr Vec2 kLeft{-1.0f, 0.0f};
constexpr Vec2 kRight{1.0f, 0.0f};

bool close(f32 a, f32 b, f32 tolerance = 1e-3f)
{
    return std::abs(a - b) <= tolerance;
}

bool close(const UiRect& a, const UiRect& b)
{
    return close(a.min.x, b.min.x) && close(a.min.y, b.min.y) && close(a.max.x, b.max.x) &&
           close(a.max.y, b.max.y);
}

// A World with a full-screen canvas and its UiWorld.
struct Menu
{
    explicit Menu(const UiWorldDesc& desc = {})
        : ui(world, desc)
    {
        canvas = world.create(fullCanvas());
    }

    template <class... Ts>
    Entity child(Entity parent, Ts&&... components)
    {
        const Entity entity = world.create(std::forward<Ts>(components)...);
        REQUIRE(setParent(world, entity, parent).hasValue());
        return entity;
    }

    Entity button(Entity parent, Vec2 min, Vec2 max)
    {
        return child(parent, placed(min, max), solid(), UiInteractable{}, UiButton{});
    }

    void step(const UiInput& input = {}, UVec2 target = kHd)
    {
        updateTransformHierarchy(world, index);
        ui.update({target}, input);
    }

    // Presses and releases the pointer at @p pixels.
    void click(Vec2 pixels)
    {
        step(pointerAt(pixels, true));
        step(pointerAt(pixels, false));
    }

    // Moves the focus once in @p direction and lets go.
    void navigate(Vec2 direction)
    {
        step(navigating(direction));
        step(navigating({}));
    }

    [[nodiscard]] u32 count(UiEventType type, Entity entity = {}) const
    {
        u32 found = 0;
        for (const UiEvent& event : ui.events())
        {
            found += event.type == type && (entity.isNull() || event.entity == entity) ? 1u : 0u;
        }
        return found;
    }

    [[nodiscard]] UiInteraction state(Entity entity) const { return world.get<UiInteraction>(entity); }

    [[nodiscard]] UiRect rectOf(Entity entity) const { return world.get<UiComputedRect>(entity).rect; }

    World world;
    HierarchyIndex index;
    UiWorld ui;
    Entity canvas{};
};

} // namespace

TEST_CASE("the pointer hovers, presses and clicks a button")
{
    Menu menu;
    const Entity button = menu.button(menu.canvas, {100.0f, 100.0f}, {300.0f, 200.0f});
    menu.step(pointerAt({50.0f, 50.0f}));
    CHECK_FALSE(menu.state(button).hovered);

    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK(menu.state(button).hovered);
    CHECK_FALSE(menu.state(button).pressed);
    CHECK(menu.ui.events().empty());

    menu.step(pointerAt({150.0f, 150.0f}, true));
    CHECK(menu.state(button).pressed);
    CHECK(menu.state(button).focused);
    REQUIRE(menu.count(UiEventType::FocusChanged, button) == 1);
    CHECK(menu.ui.events()[0].value == 0.0f);
    CHECK(menu.count(UiEventType::Clicked) == 0);

    menu.step(pointerAt({160.0f, 160.0f}, false));
    CHECK(menu.count(UiEventType::Clicked, button) == 1);
    CHECK_FALSE(menu.state(button).pressed);
    CHECK(menu.ui.focused() == button);

    UiInput gone = pointerAt({150.0f, 150.0f});
    gone.pointerPresent = false;
    menu.step(gone);
    CHECK_FALSE(menu.state(button).hovered);
}

TEST_CASE("pointer pixels are mapped to the units of each canvas")
{
    Menu menu;
    const Entity button = menu.button(menu.canvas, {300.0f, 300.0f}, {400.0f, 400.0f});
    // 1280x720 shows the 1920x1080 reference at two thirds of a pixel per unit.
    menu.step(pointerAt({210.0f, 210.0f}), {1280, 720});
    CHECK(menu.state(button).hovered);
    menu.step(pointerAt({190.0f, 190.0f}), {1280, 720});
    CHECK_FALSE(menu.state(button).hovered);
}

TEST_CASE("releasing the pointer outside the pressed element does not click it")
{
    Menu menu;
    const Entity button = menu.button(menu.canvas, {100.0f, 100.0f}, {300.0f, 200.0f});
    const Entity other = menu.button(menu.canvas, {400.0f, 100.0f}, {600.0f, 200.0f});
    menu.step(pointerAt({150.0f, 150.0f}, true));
    menu.step(pointerAt({450.0f, 150.0f}, true));
    CHECK_FALSE(menu.state(button).pressed); // still captured, but the pointer left it
    CHECK(menu.state(other).hovered);
    CHECK_FALSE(menu.state(other).pressed);
    menu.step(pointerAt({450.0f, 150.0f}, false));
    CHECK(menu.count(UiEventType::Clicked) == 0);

    // Pressing outside and releasing over a button does not click it either.
    menu.step(pointerAt({50.0f, 50.0f}, true));
    menu.step(pointerAt({150.0f, 150.0f}, false));
    CHECK(menu.count(UiEventType::Clicked) == 0);
}

TEST_CASE("the topmost interactable element wins; disabled and plain elements do not block")
{
    Menu menu;
    const Entity below = menu.button(menu.canvas, {100.0f, 100.0f}, {300.0f, 300.0f});
    const Entity above = menu.button(menu.canvas, {200.0f, 200.0f}, {400.0f, 400.0f});
    menu.step(pointerAt({250.0f, 250.0f}));
    CHECK(menu.state(above).hovered);
    CHECK_FALSE(menu.state(below).hovered);

    menu.world.get<UiInteractable>(above).enabled = false;
    menu.world.markChanged<UiInteractable>();
    menu.step(pointerAt({250.0f, 250.0f}));
    CHECK(menu.state(below).hovered);
    CHECK_FALSE(menu.state(above).hovered);
    menu.click({250.0f, 250.0f});
    CHECK(menu.count(UiEventType::Clicked, below) == 1);

    // A panel without UiInteractable on top lets the pointer through.
    (void)menu.child(menu.canvas, placed({0.0f, 0.0f}, {1920.0f, 1080.0f}), solid());
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK(menu.state(below).hovered);

    // A later canvas is on top of an earlier one, whatever the Entity order.
    const Entity overlay = menu.world.create(fullCanvas(1));
    const Entity top = menu.button(overlay, {0.0f, 0.0f}, {200.0f, 200.0f});
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK(menu.state(top).hovered);
    CHECK_FALSE(menu.state(below).hovered);
}

TEST_CASE("hit tests go through each element's transform and its ancestors' clips")
{
    Menu menu;
    UiElement scaled = placed({100.0f, 100.0f}, {300.0f, 300.0f});
    scaled.scale = {0.5f, 0.5f}; // drawn from 150 to 250
    const Entity small = menu.child(menu.canvas, scaled, solid(), UiInteractable{}, UiButton{});
    menu.step(pointerAt({120.0f, 120.0f}));
    CHECK_FALSE(menu.state(small).hovered);
    menu.step(pointerAt({240.0f, 240.0f}));
    CHECK(menu.state(small).hovered);

    UiElement turned = placed({500.0f, 100.0f}, {700.0f, 120.0f});
    turned.rotation = std::numbers::pi_v<f32> * 0.5f; // a vertical bar from y 10 to 210 at x 600
    const Entity bar = menu.child(menu.canvas, turned, solid(), UiInteractable{}, UiButton{});
    menu.step(pointerAt({520.0f, 110.0f}));
    CHECK_FALSE(menu.state(bar).hovered);
    menu.step(pointerAt({600.0f, 30.0f}));
    CHECK(menu.state(bar).hovered);

    UiElement clipper = placed({800.0f, 100.0f}, {1000.0f, 300.0f});
    clipper.clipChildren = true;
    const Entity frame = menu.child(menu.canvas, clipper);
    const Entity clipped = menu.button(frame, {150.0f, 0.0f}, {300.0f, 100.0f}); // 950 to 1100
    menu.step(pointerAt({975.0f, 150.0f}));
    CHECK(menu.state(clipped).hovered);
    menu.step(pointerAt({1050.0f, 150.0f}));
    CHECK_FALSE(menu.state(clipped).hovered);
}

TEST_CASE("an element with no opacity is not hit")
{
    Menu menu;
    UiElement invisible = placed({100.0f, 100.0f}, {300.0f, 300.0f});
    invisible.opacity = 0.0f;
    const Entity button = menu.child(menu.canvas, invisible, solid(), UiInteractable{}, UiButton{});
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK_FALSE(menu.state(button).hovered);
}

TEST_CASE("a toggle flips on click and on submit, and hides its first child while off")
{
    Menu menu;
    const Entity toggle = menu.child(menu.canvas, placed({100.0f, 100.0f}, {200.0f, 200.0f}), solid(),
                                     UiInteractable{}, UiToggle{});
    const Entity check = menu.child(toggle, placed({10.0f, 10.0f}, {90.0f, 90.0f}), solid());
    (void)menu.child(toggle, placed({0.0f, 0.0f}, {5.0f, 5.0f}), solid());
    menu.step();
    CHECK(menu.ui.drawData().vertices.size() == 2 * 4); // the toggle and its second child

    menu.click({150.0f, 150.0f});
    CHECK(menu.world.get<UiToggle>(toggle).on);
    REQUIRE(menu.count(UiEventType::ValueChanged, toggle) == 1);
    CHECK(menu.ui.events().back().value == 1.0f);
    CHECK(menu.ui.drawData().vertices.size() == 3 * 4); // shown in the same update
    CHECK(menu.world.has<UiComputedRect>(check));
    menu.step();
    CHECK(menu.ui.statistics().layoutReused);

    menu.step(submitting(true));
    menu.step(submitting(false));
    CHECK_FALSE(menu.world.get<UiToggle>(toggle).on);
    CHECK(menu.ui.events().back().value == 0.0f);
    CHECK(menu.ui.drawData().vertices.size() == 2 * 4);
}

TEST_CASE("a slider follows the pointer while pressed, snapped to its step, and places its fill and handle")
{
    Menu menu;
    UiSlider settings;
    settings.max = 10.0f;
    settings.step = 1.0f;
    const Entity slider = menu.child(menu.canvas, placed({100.0f, 100.0f}, {500.0f, 140.0f}), solid(),
                                     UiInteractable{}, settings);
    const Entity fill = menu.child(slider, UiElement{}, solid());
    const Entity handle = menu.child(slider, placed({-10.0f, -5.0f}, {10.0f, 45.0f}), solid());
    menu.step();
    CHECK(close(menu.rectOf(fill), {{100.0f, 100.0f}, {100.0f, 140.0f}}));
    CHECK(close(menu.rectOf(handle), {{90.0f, 95.0f}, {110.0f, 145.0f}}));

    menu.step(pointerAt({100.0f + 0.33f * 400.0f, 120.0f}, true));
    CHECK(menu.world.get<UiSlider>(slider).value == 3.0f);
    REQUIRE(menu.count(UiEventType::ValueChanged, slider) == 1);
    CHECK(menu.ui.events().back().value == 3.0f);
    CHECK(close(menu.rectOf(fill), {{100.0f, 100.0f}, {220.0f, 140.0f}}));
    CHECK(close(menu.rectOf(handle), {{210.0f, 95.0f}, {230.0f, 145.0f}}));

    menu.step(pointerAt({900.0f, 400.0f}, true)); // dragged past the end and off the slider
    CHECK(menu.world.get<UiSlider>(slider).value == 10.0f);
    CHECK(menu.state(slider).pressed);
    menu.step(pointerAt({900.0f, 400.0f}, true));
    CHECK(menu.count(UiEventType::ValueChanged) == 0); // unchanged values report nothing
    menu.step(pointerAt({900.0f, 400.0f}, false));
    CHECK_FALSE(menu.state(slider).pressed);
    menu.step();
    CHECK(menu.ui.statistics().layoutReused);
    CHECK(close(menu.rectOf(fill), {{100.0f, 100.0f}, {500.0f, 140.0f}}));
}

TEST_CASE("navigation along a focused slider changes its value, across it moves the focus")
{
    Menu menu;
    UiSlider settings;
    settings.max = 10.0f;
    settings.value = 5.0f;
    const Entity slider = menu.child(menu.canvas, placed({100.0f, 300.0f}, {500.0f, 340.0f}), solid(),
                                     UiInteractable{}, settings);
    const Entity above = menu.button(menu.canvas, {100.0f, 100.0f}, {500.0f, 140.0f});
    menu.step();
    menu.ui.setFocus(slider);
    CHECK(menu.ui.focused() == slider);

    menu.navigate(kRight);
    CHECK(close(menu.world.get<UiSlider>(slider).value, 6.0f)); // navigationStep of the range
    menu.navigate(kLeft);
    menu.navigate(kLeft);
    CHECK(close(menu.world.get<UiSlider>(slider).value, 4.0f));

    menu.world.get<UiSlider>(slider).step = 2.5f;
    menu.world.markChanged<UiSlider>();
    menu.navigate(kRight);
    CHECK(close(menu.world.get<UiSlider>(slider).value, 7.5f)); // 4 + 2.5, snapped to a multiple of 2.5

    menu.world.get<UiSlider>(slider).axis = UiAxis::Vertical;
    menu.world.markChanged<UiSlider>();
    menu.navigate(kDown);
    CHECK(close(menu.world.get<UiSlider>(slider).value, 10.0f)); // down is towards max on a vertical slider
    menu.navigate(kRight);
    CHECK(menu.ui.focused() == slider); // nothing to the right
    menu.world.get<UiSlider>(slider).axis = UiAxis::Horizontal;
    menu.world.markChanged<UiSlider>();
    menu.navigate(kUp);
    CHECK(menu.ui.focused() == above);
}

TEST_CASE("navigation moves the focus to the nearest element in four directions, or to explicit neighbours")
{
    Menu menu;
    // A 3x3 grid of buttons 200 units apart.
    Entity grid[3][3];
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const Vec2 min(200.0f + 200.0f * static_cast<f32>(column),
                           200.0f + 200.0f * static_cast<f32>(row));
            grid[row][column] = menu.button(menu.canvas, min, min + Vec2(100.0f, 100.0f));
        }
    }
    menu.step();
    menu.ui.setFocus(grid[1][1]);
    menu.navigate(kUp);
    CHECK(menu.ui.focused() == grid[0][1]);
    CHECK(menu.ui.events().empty()); // the event was in the frame that pressed
    menu.navigate(kDown);
    menu.navigate(kDown);
    CHECK(menu.ui.focused() == grid[2][1]);
    menu.navigate(kLeft);
    CHECK(menu.ui.focused() == grid[2][0]);
    menu.navigate(kLeft);
    CHECK(menu.ui.focused() == grid[2][0]); // the edge
    menu.navigate(kRight);
    menu.navigate(kRight);
    CHECK(menu.ui.focused() == grid[2][2]);

    menu.step(navigating(kUp));
    REQUIRE(menu.count(UiEventType::FocusChanged, grid[1][2]) == 1);
    CHECK(menu.ui.events()[0].value == 1.0f);
    menu.step();

    // An explicit neighbour wins over the nearest one.
    menu.world.get<UiInteractable>(grid[1][2]).up = grid[0][0];
    menu.world.markChanged<UiInteractable>();
    menu.navigate(kUp);
    CHECK(menu.ui.focused() == grid[0][0]);

    // A destroyed neighbour is treated as none.
    menu.world.get<UiInteractable>(grid[0][0]).right = grid[0][1];
    menu.world.markChanged<UiInteractable>();
    (void)destroyRecursive(menu.world, grid[0][1]);
    menu.navigate(kRight);
    CHECK(menu.ui.focused() == grid[0][2]);

    // A neighbour that cannot take the focus is skipped too.
    menu.world.get<UiInteractable>(grid[0][2]).down = grid[2][2];
    menu.world.get<UiInteractable>(grid[2][2]).focusable = false;
    menu.world.markChanged<UiInteractable>();
    menu.navigate(kDown);
    CHECK(menu.ui.focused() == grid[1][2]);
}

TEST_CASE("held navigation repeats after a delay, then at an interval")
{
    Menu menu;
    std::vector<Entity> row;
    for (int i = 0; i < 12; ++i)
    {
        const Vec2 min(50.0f + 150.0f * static_cast<f32>(i), 100.0f);
        row.push_back(menu.button(menu.canvas, min, min + Vec2(100.0f, 50.0f)));
    }
    menu.step();
    menu.ui.setFocus(row[0]);
    std::vector<int> moves;
    for (int frame = 0; frame < 23; ++frame)
    {
        menu.step(navigating(kRight, 0.03));
        if (menu.count(UiEventType::FocusChanged) > 0)
        {
            moves.push_back(frame);
        }
    }
    // The first move at once, the first repeat after 0.4 s, then every 0.12 s.
    CHECK(moves == std::vector<int>{0, 14, 18, 22});

    // A deflection under the threshold is no direction; letting go and pushing again moves at once.
    menu.step(navigating({0.4f, 0.0f}, 0.03));
    CHECK(menu.count(UiEventType::FocusChanged) == 0);
    menu.step(navigating(kRight, 0.03));
    CHECK(menu.count(UiEventType::FocusChanged) == 1);
}

TEST_CASE(
    "with nothing focused, the first navigation or submit focuses the first element of the topmost canvas")
{
    Menu menu;
    (void)menu.button(menu.canvas, {100.0f, 100.0f}, {200.0f, 200.0f});
    const Entity overlay = menu.world.create(fullCanvas(1));
    const Entity hidden =
        menu.child(overlay, placed({0.0f, 0.0f}, {50.0f, 50.0f}), solid(), UiInteractable{});
    menu.world.get<UiInteractable>(hidden).focusable = false;
    const Entity first = menu.button(overlay, {900.0f, 900.0f}, {1000.0f, 1000.0f});
    (void)menu.button(overlay, {100.0f, 900.0f}, {200.0f, 1000.0f});
    menu.step();
    CHECK(menu.ui.focused().isNull());

    menu.step(navigating(kLeft));
    CHECK(menu.ui.focused() == first);
    CHECK(menu.count(UiEventType::FocusChanged, first) == 1);
    menu.step();

    menu.ui.setFocus({});
    menu.step(submitting(true));
    CHECK(menu.ui.focused() == first);
    menu.step(submitting(false));
    CHECK(menu.count(UiEventType::Clicked) == 0); // the submit that focuses does not click
    menu.step(submitting(true));
    CHECK(menu.state(first).pressed);
    menu.step(submitting(false));
    CHECK(menu.count(UiEventType::Clicked, first) == 1);
}

TEST_CASE("only the topmost modal canvas and those above it can be hit or focused")
{
    Menu menu;
    const Entity hud = menu.button(menu.canvas, {100.0f, 100.0f}, {200.0f, 200.0f});
    const Entity pause = menu.world.create(fullCanvas(1, true));
    menu.step();
    CHECK_FALSE(menu.ui.modalActive()); // a modal canvas without elements does not count
    menu.ui.setFocus(hud);
    CHECK(menu.ui.focused() == hud);

    const Entity resume = menu.button(pause, {800.0f, 400.0f}, {1100.0f, 480.0f});
    const Entity quit = menu.button(pause, {800.0f, 500.0f}, {1100.0f, 580.0f});
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK(menu.ui.modalActive());
    CHECK(menu.ui.focused().isNull()); // the HUD left the canvases that can be focused
    CHECK_FALSE(menu.state(hud).hovered);
    menu.ui.setFocus(hud);
    CHECK(menu.ui.focused().isNull());

    menu.navigate(kDown);
    CHECK(menu.ui.focused() == resume);
    menu.navigate(kDown);
    CHECK(menu.ui.focused() == quit);
    menu.navigate(kDown);
    CHECK(menu.ui.focused() == quit);

    // A canvas above the modal one still takes input.
    const Entity toast = menu.world.create(fullCanvas(2));
    const Entity dismiss = menu.button(toast, {100.0f, 100.0f}, {200.0f, 200.0f});
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK(menu.state(dismiss).hovered);

    setEnabled(menu.world, pause, false);
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK_FALSE(menu.ui.modalActive());
    CHECK(menu.ui.focused().isNull());
}

TEST_CASE("cancel reports the canvas of the focused element, else the modal canvas, else none")
{
    Menu menu;
    const Entity button = menu.button(menu.canvas, {100.0f, 100.0f}, {200.0f, 200.0f});
    UiInput cancel;
    cancel.cancelPressed = true;
    menu.step(cancel);
    REQUIRE(menu.count(UiEventType::Cancelled) == 1);
    CHECK(menu.ui.events()[0].entity.isNull());

    menu.ui.setFocus(button);
    menu.step(cancel);
    CHECK(menu.count(UiEventType::Cancelled, menu.canvas) == 1);

    const Entity pause = menu.world.create(fullCanvas(1, true));
    (void)menu.button(pause, {800.0f, 400.0f}, {1100.0f, 480.0f});
    menu.step(cancel);
    CHECK(menu.count(UiEventType::Cancelled, pause) == 1);
}

TEST_CASE("the focus is dropped when its element is destroyed, deactivated or disabled")
{
    Menu menu;
    const Entity a = menu.button(menu.canvas, {100.0f, 100.0f}, {200.0f, 200.0f});
    const Entity b = menu.button(menu.canvas, {300.0f, 100.0f}, {400.0f, 200.0f});
    const Entity c = menu.button(menu.canvas, {500.0f, 100.0f}, {600.0f, 200.0f});
    menu.step();

    menu.ui.setFocus(a);
    menu.step();
    CHECK(menu.state(a).focused);
    (void)destroyRecursive(menu.world, a);
    menu.step();
    CHECK(menu.ui.focused().isNull());

    menu.ui.setFocus(b);
    menu.step();
    setEnabled(menu.world, b, false);
    menu.step();
    CHECK(menu.ui.focused().isNull());
    CHECK_FALSE(menu.state(b).focused); // not laid out, so its state is cleared

    menu.ui.setFocus(c);
    menu.step();
    menu.world.get<UiInteractable>(c).enabled = false;
    menu.step(); // without markChanged too: the state is read every update
    CHECK(menu.ui.focused().isNull());
    CHECK_FALSE(menu.state(c).focused);
    menu.ui.setFocus(c);
    CHECK(menu.ui.focused().isNull()); // a disabled element cannot take it
}

TEST_CASE("events beyond maxEvents are dropped and counted")
{
    UiWorldDesc desc;
    desc.maxEvents = 2;
    Menu menu(desc);
    const Entity button = menu.button(menu.canvas, {100.0f, 100.0f}, {200.0f, 200.0f});
    menu.step(pointerAt({150.0f, 150.0f}, true)); // FocusChanged
    UiInput release = pointerAt({150.0f, 150.0f}, false);
    release.cancelPressed = true;
    menu.ui.setFocus({});
    release.navigate = kRight; // Clicked, then FocusChanged as navigation focuses it again, then Cancelled
    menu.step(release);
    CHECK(menu.ui.events().size() == 2);
    CHECK(menu.ui.statistics().droppedEvents == 1);
    CHECK(menu.count(UiEventType::Clicked, button) == 1);
    menu.step();
    CHECK(menu.ui.statistics().droppedEvents == 0);
}

TEST_CASE("the image tint is multiplied by the tint of the state: disabled, pressed, focused, hovered")
{
    Menu menu;
    UiInteractable tints;
    tints.hoveredTint = {1.0f, 1.0f, 1.0f, 0.25f};
    tints.focusedTint = {1.0f, 1.0f, 1.0f, 0.5f};
    tints.pressedTint = {1.0f, 1.0f, 1.0f, 0.75f};
    tints.disabledTint = {0.0f, 0.0f, 0.0f, 1.0f};
    const Entity button = menu.child(menu.canvas, placed({100.0f, 100.0f}, {200.0f, 200.0f}),
                                     solid({1.0f, 0.0f, 0.0f, 1.0f}), tints, UiButton{});
    const auto colour = [&] {
        return menu.ui.drawData().vertices[0].color;
    };
    menu.step();
    CHECK(colour() == 0xFF0000FFu);
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK((colour() >> 24) == 64u);
    menu.step(pointerAt({150.0f, 150.0f}, true));
    CHECK((colour() >> 24) == 191u); // pressed over focused and hovered
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK((colour() >> 24) == 128u); // focused over hovered
    menu.world.get<UiInteractable>(button).enabled = false;
    menu.step(pointerAt({150.0f, 150.0f}));
    CHECK(colour() == 0xFF000000u);
}

TEST_CASE("the interaction Components survive a scene save and load with their neighbours")
{
    registerUiComponents();
    reflectCoreComponents();
    World world;
    const Entity first = world.create(UiElement{}, UiInteractable{}, UiButton{});
    UiInteractable linked;
    linked.enabled = false;
    linked.focusable = false;
    linked.hoveredTint = {0.1f, 0.2f, 0.3f, 0.4f};
    linked.pressedTint = {0.5f, 0.6f, 0.7f, 0.8f};
    linked.focusedTint = {0.9f, 1.0f, 1.1f, 1.2f};
    linked.disabledTint = {0.2f, 0.3f, 0.4f, 0.5f};
    linked.up = first;
    linked.down = first;
    linked.left = first;
    linked.right = first;
    UiToggle toggle;
    toggle.on = true;
    const Entity second = world.create(UiElement{}, linked, toggle);
    world.get<UiInteractable>(first).right = second;
    UiSlider slider;
    slider.value = 2.0f;
    slider.min = -1.0f;
    slider.max = 4.0f;
    slider.step = 0.5f;
    slider.axis = UiAxis::Vertical;
    slider.navigationStep = 0.25f;
    (void)world.create(UiElement{}, UiInteractable{}, slider, UiInteraction{});
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json.find("illusion.UiInteraction\"") == std::string::npos);

    World loaded;
    REQUIRE(loadScene(loaded, saved.json).ok());
    Entity button{};
    Entity other{};
    loaded.query<const UiInteractable>().each([&](Entity entity, const UiInteractable&) {
        if (loaded.has<UiButton>(entity))
        {
            button = entity;
        }
        if (const UiToggle* t = loaded.tryGet<UiToggle>(entity))
        {
            other = entity;
            CHECK(t->on);
        }
    });
    REQUIRE_FALSE(button.isNull());
    REQUIRE_FALSE(other.isNull());
    CHECK(loaded.get<UiInteractable>(button).right == other);
    const UiInteractable& back = loaded.get<UiInteractable>(other);
    CHECK_FALSE(back.enabled);
    CHECK_FALSE(back.focusable);
    CHECK(back.hoveredTint == linked.hoveredTint);
    CHECK(back.pressedTint == linked.pressedTint);
    CHECK(back.focusedTint == linked.focusedTint);
    CHECK(back.disabledTint == linked.disabledTint);
    CHECK(back.up == button);
    CHECK(back.down == button);
    CHECK(back.left == button);
    CHECK(back.right == button);
    u32 sliders = 0;
    loaded.query<const UiSlider>().each([&](Entity, const UiSlider& s) {
        ++sliders;
        CHECK(s.value == slider.value);
        CHECK(s.min == slider.min);
        CHECK(s.max == slider.max);
        CHECK(s.step == slider.step);
        CHECK(s.axis == slider.axis);
        CHECK(s.navigationStep == slider.navigationStep);
    });
    CHECK(sliders == 1);
}

TEST_CASE("a steady update with input allocates nothing and reuses the layout")
{
    Menu menu;
    std::vector<Entity> buttons;
    for (int i = 0; i < 8; ++i)
    {
        const Vec2 min(100.0f + 120.0f * static_cast<f32>(i), 100.0f);
        buttons.push_back(menu.button(menu.canvas, min, min + Vec2(100.0f, 60.0f)));
    }
    const Entity toggle = menu.child(menu.canvas, placed({100.0f, 300.0f}, {160.0f, 360.0f}), solid(),
                                     UiInteractable{}, UiToggle{});
    (void)menu.child(toggle, placed({5.0f, 5.0f}, {55.0f, 55.0f}), solid());
    const Entity slider = menu.child(menu.canvas, placed({300.0f, 300.0f}, {700.0f, 340.0f}), solid(),
                                     UiInteractable{}, UiSlider{});
    (void)menu.child(slider, UiElement{}, solid());
    (void)menu.child(slider, placed({-8.0f, 0.0f}, {8.0f, 40.0f}), solid());
    menu.click({150.0f, 130.0f});
    menu.step(navigating(kRight));
    menu.step(navigating({}));
    const UiInput resting = pointerAt({150.0f, 130.0f});
    menu.step(resting);
    menu.step(resting);
    REQUIRE(menu.ui.statistics().layoutReused);
    updateTransformHierarchy(menu.world, menu.index);
    {
        const test::ScopedAllocationCounter counter;
        menu.ui.update({kHd}, resting);
        CHECK(counter.count() == 0);
    }
    CHECK(menu.ui.statistics().layoutReused);
    CHECK(menu.state(buttons[0]).hovered);

    // Values that change by input do not allocate either.
    UiInput drag = pointerAt({500.0f, 320.0f}, true);
    {
        const test::ScopedAllocationCounter counter;
        menu.ui.update({kHd}, drag);
        drag.pointerPixels.x = 600.0f;
        menu.ui.update({kHd}, drag);
        CHECK(counter.count() == 0);
    }
    CHECK(menu.world.get<UiSlider>(slider).value > 0.6f);
}
