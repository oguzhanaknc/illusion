// The GPU-free math of canvas editing (ADR 0057, addendum UI-3d): anchor presets, placements, picking in a
// layout, handle hit tests and drags.

#include <illusion/editor/core/UiPlacementEdit.hpp>

#include <doctest/doctest.h>

#include <limits>
#include <numbers>

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr f32 kNaN = std::numeric_limits<f32>::quiet_NaN();

void checkNear(Vec2 actual, Vec2 expected)
{
    CHECK(static_cast<f64>(actual.x) == doctest::Approx(static_cast<f64>(expected.x)).epsilon(1e-4));
    CHECK(static_cast<f64>(actual.y) == doctest::Approx(static_cast<f64>(expected.y)).epsilon(1e-4));
}

void checkNear(const UiRect& actual, const UiRect& expected)
{
    checkNear(actual.min, expected.min);
    checkNear(actual.max, expected.max);
}

UiPlacement fixedAt(Vec2 anchor, Vec2 min, Vec2 max)
{
    UiPlacement placement;
    placement.anchorMin = placement.anchorMax = anchor;
    placement.offsetMin = min;
    placement.offsetMax = max;
    return placement;
}

UiLayoutElement element(u32 index, UiRect rect, u32 parent = UiLayoutElement::kNoParent)
{
    UiLayoutElement out;
    out.entity = Entity{index, 1};
    out.parent = parent;
    out.rect = rect;
    return out;
}

UiLayout layoutOf(std::initializer_list<UiLayoutElement> elements)
{
    UiLayout layout;
    UiLayoutCanvas canvas;
    canvas.area = {{0.0f, 0.0f}, {1920.0f, 1080.0f}};
    layout.canvases.push_back(canvas);
    layout.elements.assign(elements.begin(), elements.end());
    return layout;
}

// A placed element at (100, 100)-(300, 200) of a 1920x1080 canvas, anchored to its top left.
UiHandleFrame placedFrame()
{
    UiHandleFrame frame;
    frame.rect = {{100.0f, 100.0f}, {300.0f, 200.0f}};
    frame.parent = {{0.0f, 0.0f}, {1920.0f, 1080.0f}};
    frame.placement = fixedAt({0.0f, 0.0f}, {100.0f, 100.0f}, {300.0f, 200.0f});
    frame.size = {200.0f, 100.0f};
    return frame;
}

} // namespace

TEST_CASE("every anchor preset round-trips through its anchors, and other anchors are no preset")
{
    for (u32 i = 0; i < kUiAnchorPresetCount; ++i)
    {
        CAPTURE(i);
        const auto preset = static_cast<UiAnchorPreset>(i);
        const UiAnchors anchors = anchorsOf(preset);
        UiPlacement placement;
        placement.anchorMin = anchors.min;
        placement.anchorMax = anchors.max;
        placement.offsetMin = {3.0f, 4.0f};
        REQUIRE(anchorPresetOf(placement).has_value());
        CHECK(*anchorPresetOf(placement) == preset);
    }
    CHECK(anchorsOf(UiAnchorPreset::Stretch).max == Vec2(1.0f));
    CHECK(anchorsOf(UiAnchorPreset::StretchCenter).min == Vec2(0.5f, 0.0f));
    CHECK(anchorsOf(UiAnchorPreset::BottomRight).min == Vec2(1.0f));
    UiPlacement odd;
    odd.anchorMin = {0.25f, 0.0f};
    odd.anchorMax = {0.25f, 0.0f};
    CHECK_FALSE(anchorPresetOf(odd).has_value());
}

TEST_CASE("withAnchors keeps the rectangle for any parent size, or keeps the offsets without keepRect")
{
    const UiPlacement centred = fixedAt({0.5f, 0.5f}, {-100.0f, -50.0f}, {100.0f, 50.0f});
    const UiRect parents[] = {{{0.0f, 0.0f}, {1920.0f, 1080.0f}},
                              {{10.0f, 20.0f}, {1290.0f, 740.0f}},
                              {{-50.0f, 0.0f}, {350.0f, 300.0f}}};
    const UiAnchors targets[] = {
        anchorsOf(UiAnchorPreset::TopLeft), anchorsOf(UiAnchorPreset::Stretch), {{0.2f, 0.1f}, {0.9f, 0.8f}}};
    for (const UiRect& parent : parents)
    {
        for (const UiAnchors& target : targets)
        {
            const UiPlacement moved = withAnchors(centred, parent, target, true);
            CHECK(moved.anchorMin == target.min);
            CHECK(moved.anchorMax == target.max);
            checkNear(resolvePlacement(moved, parent), resolvePlacement(centred, parent));
            // And back from stretched anchors.
            const UiPlacement back = withAnchors(moved, parent, anchorsOf(UiAnchorPreset::Center), true);
            checkNear(back.offsetMin, centred.offsetMin);
            checkNear(back.offsetMax, centred.offsetMax);
        }
    }
    const UiPlacement kept = withAnchors(centred, parents[0], anchorsOf(UiAnchorPreset::TopRight), false);
    CHECK(kept.offsetMin == centred.offsetMin);
    CHECK(kept.offsetMax == centred.offsetMax);
    CHECK(kept.anchorMin == Vec2(1.0f, 0.0f));
}

TEST_CASE("withAnchors keeps anchors inside the parent and max not below min, and ignores non-finite input")
{
    const UiPlacement centred = fixedAt({0.5f, 0.5f}, {-100.0f, -50.0f}, {100.0f, 50.0f});
    const UiRect parent{{0.0f, 0.0f}, {1000.0f, 500.0f}};
    const UiPlacement clamped = withAnchors(centred, parent, {{-1.0f, 2.0f}, {0.5f, 0.2f}}, true);
    CHECK(clamped.anchorMin == Vec2(0.0f, 1.0f));
    CHECK(clamped.anchorMax == Vec2(0.5f, 1.0f));
    checkNear(resolvePlacement(clamped, parent), resolvePlacement(centred, parent));
    CHECK(withAnchors(centred, parent, {{kNaN, 0.0f}, {1.0f, 1.0f}}, true) == centred);
    CHECK(withAnchors(centred, {{0.0f, 0.0f}, {kNaN, 1.0f}}, anchorsOf(UiAnchorPreset::Stretch), true) ==
          centred);
}

TEST_CASE("placementFor reaches a rectangle with the anchors it has")
{
    const UiRect parent{{10.0f, 20.0f}, {1290.0f, 740.0f}};
    UiPlacement stretched = fixedAt({0.0f, 0.0f}, {}, {});
    stretched.anchorMax = {1.0f, 0.5f};
    const UiRect target{{100.0f, 60.0f}, {400.0f, 90.0f}};
    const UiPlacement placed = placementFor(stretched, parent, target);
    CHECK(placed.anchorMin == stretched.anchorMin);
    CHECK(placed.anchorMax == stretched.anchorMax);
    checkNear(resolvePlacement(placed, parent), target);
    // An inverted rectangle is empty at its min.
    const UiPlacement empty = placementFor(stretched, parent, {{100.0f, 60.0f}, {50.0f, 40.0f}});
    checkNear(resolvePlacement(empty, parent), {{100.0f, 60.0f}, {100.0f, 60.0f}});
    CHECK(placementFor(stretched, parent, {{kNaN, 0.0f}, {1.0f, 1.0f}}) == stretched);
}

TEST_CASE("pickUiElement finds the topmost visible element through transforms and clips")
{
    UiLayout layout = layoutOf(
        {element(1, {{0.0f, 0.0f}, {100.0f, 100.0f}}), element(2, {{10.0f, 10.0f}, {50.0f, 50.0f}}, 0),
         element(3, {{500.0f, 0.0f}, {700.0f, 20.0f}}), element(4, {{0.0f, 200.0f}, {100.0f, 300.0f}}),
         element(5, {{50.0f, 250.0f}, {200.0f, 400.0f}}, 3)});
    // Later elements are drawn on top.
    CHECK(pickUiElement(layout, {20.0f, 20.0f}) == Entity{2, 1});
    CHECK(pickUiElement(layout, {80.0f, 80.0f}) == Entity{1, 1});
    layout.elements[1].visible = false;
    CHECK(pickUiElement(layout, {20.0f, 20.0f}) == Entity{1, 1});

    // Element 3 turned a quarter about its centre stands upright: (600, -90)-(600, 110) wide 20.
    layout.elements[2].transform =
        makeUiTransform({600.0f, 10.0f}, std::numbers::pi_v<f32> / 2.0f, Vec2(1.0f));
    CHECK(pickUiElement(layout, {600.0f, 80.0f}) == Entity{3, 1});
    CHECK(pickUiElement(layout, {680.0f, 10.0f}).isNull());

    // Element 5 sticks out of element 4, which clips it only once it clips its children.
    CHECK(pickUiElement(layout, {150.0f, 350.0f}) == Entity{5, 1});
    layout.elements[3].clipsChildren = true;
    CHECK(pickUiElement(layout, {150.0f, 350.0f}).isNull());
    CHECK(pickUiElement(layout, {75.0f, 275.0f}) == Entity{5, 1});

    CHECK(pickUiElement(layout, {1500.0f, 900.0f}).isNull());
    CHECK(pickUiElement(layout, {kNaN, 20.0f}).isNull());
    CHECK(pickUiElement(UiLayout{}, {20.0f, 20.0f}).isNull());
}

TEST_CASE("handleFrameOf takes the parent's rectangle and transform, or the canvas area")
{
    UiLayout layout = layoutOf({element(1, {{100.0f, 100.0f}, {500.0f, 400.0f}}),
                                element(2, {{120.0f, 120.0f}, {200.0f, 160.0f}}, 0)});
    layout.elements[0].transform = makeUiTransform({300.0f, 250.0f}, 0.5f, Vec2(1.0f));
    layout.elements[1].transform = layout.elements[0].transform;
    const UiPlacement placement = fixedAt({0.0f, 0.0f}, {20.0f, 20.0f}, {100.0f, 60.0f});

    const std::optional<UiHandleFrame> child = handleFrameOf(layout, 1, placement, {80.0f, 40.0f}, true);
    REQUIRE(child.has_value());
    CHECK(child->parent == layout.elements[0].rect);
    CHECK(child->parentTransform == layout.elements[0].transform);
    CHECK(child->rect == layout.elements[1].rect);
    CHECK(child->size == Vec2(80.0f, 40.0f));

    const std::optional<UiHandleFrame> root = handleFrameOf(layout, 0, placement, {}, false);
    REQUIRE(root.has_value());
    CHECK(root->parent == layout.canvases[0].area);
    CHECK(root->parentTransform == UiTransform2D{});
    CHECK_FALSE(root->placed);
    CHECK_FALSE(handleFrameOf(layout, 2, placement, {}, true).has_value());

    const std::array<Vec2, 4> corners = handleCorners(*child);
    checkNear(corners[2], layout.elements[1].transform.apply({200.0f, 160.0f}));
    const UiAnchors markers = anchorMarkers(*child);
    checkNear(markers.min, layout.elements[0].transform.apply({100.0f, 100.0f}));
}

TEST_CASE("handle hit tests prefer anchors to corners, corners to edges and edges to the body")
{
    UiHandleFrame frame = placedFrame();
    CHECK(uiHandleHitTest(frame, {101.0f, 102.0f}, 8.0f) == UiHandle::TopLeft);
    CHECK(uiHandleHitTest(frame, {299.0f, 199.0f}, 8.0f) == UiHandle::BottomRight);
    CHECK(uiHandleHitTest(frame, {300.0f, 100.0f}, 8.0f) == UiHandle::TopRight);
    CHECK(uiHandleHitTest(frame, {100.0f, 200.0f}, 8.0f) == UiHandle::BottomLeft);
    CHECK(uiHandleHitTest(frame, {200.0f, 97.0f}, 8.0f) == UiHandle::Top);
    CHECK(uiHandleHitTest(frame, {104.0f, 150.0f}, 8.0f) == UiHandle::Left);
    CHECK(uiHandleHitTest(frame, {300.0f, 150.0f}, 8.0f) == UiHandle::Right);
    CHECK(uiHandleHitTest(frame, {200.0f, 205.0f}, 8.0f) == UiHandle::Bottom);
    CHECK(uiHandleHitTest(frame, {200.0f, 150.0f}, 8.0f) == UiHandle::Body);
    CHECK(uiHandleHitTest(frame, {600.0f, 600.0f}, 8.0f) == UiHandle::None);
    CHECK(uiHandleHitTest(frame, {2.0f, 2.0f}, 8.0f) == UiHandle::Anchors);

    // The anchor marker wins over a corner at the same place.
    frame.rect = {{0.0f, 0.0f}, {200.0f, 100.0f}};
    frame.placement = fixedAt({0.0f, 0.0f}, {}, {200.0f, 100.0f});
    CHECK(uiHandleHitTest(frame, {1.0f, 1.0f}, 8.0f) == UiHandle::Anchors);
    frame.placement.anchorMax = Vec2(1.0f);
    CHECK(uiHandleHitTest(frame, {1.0f, 1.0f}, 8.0f) == UiHandle::AnchorMin);
    CHECK(uiHandleHitTest(frame, {1918.0f, 1079.0f}, 8.0f) == UiHandle::AnchorMax);

    // An element its parent places has no anchors and no body.
    frame.placed = false;
    CHECK(uiHandleHitTest(frame, {1.0f, 1.0f}, 8.0f) == UiHandle::TopLeft);
    CHECK(uiHandleHitTest(frame, {100.0f, 50.0f}, 8.0f) == UiHandle::None);

    CHECK(uiHandleHitTest(frame, {kNaN, 1.0f}, 8.0f) == UiHandle::None);
    CHECK(uiHandleHitTest(frame, {1.0f, 1.0f}, kNaN) == UiHandle::None);
}

TEST_CASE("handle hit tests follow a rotated element")
{
    UiHandleFrame frame = placedFrame();
    frame.transform = makeUiTransform({200.0f, 150.0f}, std::numbers::pi_v<f32> / 2.0f, Vec2(1.0f));
    const std::array<Vec2, 4> corners = handleCorners(frame);
    CHECK(uiHandleHitTest(frame, corners[0], 4.0f) == UiHandle::TopLeft);
    CHECK(uiHandleHitTest(frame, (corners[1] + corners[2]) * 0.5f, 4.0f) == UiHandle::Right);
    // Inside the unrotated rectangle but outside the turned one.
    CHECK(uiHandleHitTest(frame, {280.0f, 150.0f}, 4.0f) == UiHandle::None);
    CHECK(uiHandleHitTest(frame, {200.0f, 220.0f}, 4.0f) == UiHandle::Body);
}

TEST_CASE("dragging the body moves the offsets by the pointer's movement in the parent's space")
{
    UiHandleFrame frame = placedFrame();
    frame.parentTransform = makeUiTransform({0.0f, 0.0f}, std::numbers::pi_v<f32> / 2.0f, Vec2(2.0f));
    frame.transform = frame.parentTransform;
    UiHandleDrag drag;
    CHECK(drag.handle() == UiHandle::None);
    REQUIRE(drag.begin(frame, UiHandle::Body, frame.parentTransform.apply({150.0f, 150.0f})));
    CHECK(drag.handle() == UiHandle::Body);
    const UiHandleEdit edit = drag.update(frame.parentTransform.apply({160.0f, 130.0f}), 0.0f);
    checkNear(edit.placement.offsetMin, {110.0f, 80.0f});
    checkNear(edit.placement.offsetMax, {310.0f, 180.0f});
    CHECK(edit.placement.anchorMin == frame.placement.anchorMin);
    CHECK(edit.size == frame.size);

    // Snapped to 10 units, the rectangle keeps its size.
    const UiHandleEdit snapped = drag.update(frame.parentTransform.apply({163.0f, 156.0f}), 10.0f);
    checkNear(snapped.placement.offsetMin, {110.0f, 110.0f});
    checkNear(snapped.placement.offsetMax, {310.0f, 210.0f});
}

TEST_CASE("dragging each edge and corner of a rotated element moves the edges it holds")
{
    UiHandleFrame frame = placedFrame();
    frame.transform = makeUiTransform({200.0f, 150.0f}, 0.7f, Vec2(1.5f, 0.5f));
    struct Case
    {
        UiHandle handle;
        Vec2 at;    // where it is grabbed, in the element's own space
        Vec4 moves; // which of left, top, right, bottom it moves
    };
    const Case cases[] = {
        {UiHandle::Left, {100.0f, 150.0f}, {1, 0, 0, 0}},
        {UiHandle::Top, {200.0f, 100.0f}, {0, 1, 0, 0}},
        {UiHandle::Right, {300.0f, 150.0f}, {0, 0, 1, 0}},
        {UiHandle::Bottom, {200.0f, 200.0f}, {0, 0, 0, 1}},
        {UiHandle::TopLeft, {100.0f, 100.0f}, {1, 1, 0, 0}},
        {UiHandle::TopRight, {300.0f, 100.0f}, {0, 1, 1, 0}},
        {UiHandle::BottomRight, {300.0f, 200.0f}, {0, 0, 1, 1}},
        {UiHandle::BottomLeft, {100.0f, 200.0f}, {1, 0, 0, 1}},
    };
    const Vec2 delta{10.0f, 7.0f};
    for (const Case& c : cases)
    {
        CAPTURE(static_cast<int>(c.handle));
        UiHandleDrag drag;
        REQUIRE(drag.begin(frame, c.handle, frame.transform.apply(c.at)));
        const UiHandleEdit edit = drag.update(frame.transform.apply(c.at + delta), 0.0f);
        checkNear(edit.placement.offsetMin,
                  frame.placement.offsetMin + Vec2(c.moves.x * delta.x, c.moves.y * delta.y));
        checkNear(edit.placement.offsetMax,
                  frame.placement.offsetMax + Vec2(c.moves.z * delta.x, c.moves.w * delta.y));
        CHECK(edit.size == frame.size);
    }
}

TEST_CASE("an edge dragged past the opposite one stops there, and snapping rounds the moved edge")
{
    const UiHandleFrame frame = placedFrame();
    UiHandleDrag drag;
    REQUIRE(drag.begin(frame, UiHandle::Left, {100.0f, 150.0f}));
    const UiHandleEdit crossed = drag.update({900.0f, 150.0f}, 0.0f);
    CHECK(crossed.placement.offsetMin.x == doctest::Approx(300.0));
    CHECK(crossed.placement.offsetMax.x == doctest::Approx(300.0));
    CHECK(drag.update({113.0f, 150.0f}, 10.0f).placement.offsetMin.x == doctest::Approx(110.0));

    REQUIRE(drag.begin(frame, UiHandle::TopLeft, {100.0f, 100.0f}));
    const UiHandleEdit corner = drag.update({0.0f, 500.0f}, 0.0f);
    CHECK(corner.placement.offsetMin.x == doctest::Approx(0.0));
    CHECK(corner.placement.offsetMin.y == doctest::Approx(200.0));
}

TEST_CASE("dragging anchors keeps the rectangle and keeps them inside the parent")
{
    UiHandleFrame frame = placedFrame();
    frame.parent = {{0.0f, 0.0f}, {1000.0f, 500.0f}};
    const UiRect before = resolvePlacement(frame.placement, frame.parent);
    UiHandleDrag drag;
    REQUIRE(drag.begin(frame, UiHandle::Anchors, {0.0f, 0.0f}));
    const UiHandleEdit middle = drag.update({500.0f, 250.0f}, 0.0f);
    checkNear(middle.placement.anchorMin, {0.5f, 0.5f});
    CHECK(middle.placement.anchorMax == middle.placement.anchorMin);
    checkNear(resolvePlacement(middle.placement, frame.parent), before);
    const UiHandleEdit outside = drag.update({5000.0f, -100.0f}, 0.0f);
    CHECK(outside.placement.anchorMin == Vec2(1.0f, 0.0f));
    checkNear(resolvePlacement(outside.placement, frame.parent), before);

    frame.placement.anchorMax = Vec2(1.0f);
    frame.placement = placementFor(frame.placement, frame.parent, before);
    REQUIRE(drag.begin(frame, UiHandle::AnchorMin, {0.0f, 0.0f}));
    const UiHandleEdit pushed = drag.update({2000.0f, 250.0f}, 0.0f);
    CHECK(pushed.placement.anchorMin == Vec2(1.0f, 0.5f));
    CHECK(pushed.placement.anchorMax == Vec2(1.0f));
    checkNear(resolvePlacement(pushed.placement, frame.parent), before);
    REQUIRE(drag.begin(frame, UiHandle::AnchorMax, {1000.0f, 500.0f}));
    const UiHandleEdit pulled = drag.update({-900.0f, 400.0f}, 0.0f);
    checkNear(pulled.placement.anchorMax, {0.0f, 0.8f});
    checkNear(resolvePlacement(pulled.placement, frame.parent), before);
}

TEST_CASE("resizing an element its parent places writes its size and nothing else")
{
    UiHandleFrame frame = placedFrame();
    frame.placed = false;
    frame.size = Vec2(0.0f);
    UiHandleDrag drag;
    CHECK_FALSE(drag.begin(frame, UiHandle::Body, {200.0f, 150.0f}));
    CHECK_FALSE(drag.begin(frame, UiHandle::Anchors, {0.0f, 0.0f}));
    REQUIRE(drag.begin(frame, UiHandle::Right, {300.0f, 150.0f}));
    const UiHandleEdit wider = drag.update({320.0f, 150.0f}, 0.0f);
    CHECK(wider.size == Vec2(220.0f, 0.0f));
    CHECK(wider.placement == frame.placement);
    REQUIRE(drag.begin(frame, UiHandle::TopLeft, {100.0f, 100.0f}));
    const UiHandleEdit smaller = drag.update({150.0f, 900.0f}, 0.0f);
    CHECK(smaller.size == Vec2(150.0f, 0.0f));
}

TEST_CASE("a drag refuses non-invertible transforms, missing handles and non-finite input")
{
    UiHandleFrame frame = placedFrame();
    UiHandleDrag drag;
    CHECK_FALSE(drag.begin(frame, UiHandle::None, {100.0f, 100.0f}));
    CHECK_FALSE(drag.begin(frame, static_cast<UiHandle>(200), {100.0f, 100.0f}));
    CHECK_FALSE(drag.begin(frame, UiHandle::Left, {kNaN, 100.0f}));

    REQUIRE(drag.begin(frame, UiHandle::Left, {100.0f, 150.0f}));
    const UiHandleEdit unchanged = drag.update({kNaN, 150.0f}, 1.0f);
    CHECK(unchanged.placement == frame.placement);
    CHECK(drag.update({110.0f, 150.0f}, kNaN).placement.offsetMin.x == doctest::Approx(110.0));

    frame.transform = makeUiTransform({200.0f, 150.0f}, 0.0f, Vec2(0.0f));
    CHECK_FALSE(drag.begin(frame, UiHandle::Left, {100.0f, 150.0f}));
    CHECK(drag.handle() == UiHandle::None);
    const UiHandleEdit idle = drag.update({500.0f, 500.0f}, 0.0f);
    CHECK(idle.placement == frame.placement);
    CHECK(idle.size == frame.size);
    frame.parentTransform = frame.transform;
    CHECK_FALSE(drag.begin(frame, UiHandle::Body, {100.0f, 150.0f}));
    frame.placement.offsetMin.x = kNaN;
    frame.transform = UiTransform2D{};
    CHECK_FALSE(drag.begin(frame, UiHandle::Left, {100.0f, 150.0f}));
}
