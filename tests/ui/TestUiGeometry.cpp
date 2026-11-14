#include <illusion/ui/UiGeometry.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using namespace illusion;

namespace
{

bool near(Vec2 a, Vec2 b, f32 tolerance = 1e-4f)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
}

bool near(f32 a, f32 b, f32 tolerance = 1e-5f)
{
    return std::abs(a - b) <= tolerance;
}

UiCanvasMetrics metrics(UiScaleMode mode, UVec2 target, f32 userScale = 1.0f)
{
    UiCanvasScale scale;
    scale.mode = mode;
    scale.userScale = userScale;
    return canvasMetrics(scale, target);
}

} // namespace

TEST_CASE("intersecting rectangles gives the shared part, or an empty rectangle at the first one's corner")
{
    const UiRect a{{0.0f, 0.0f}, {100.0f, 50.0f}};
    CHECK(intersect(a, {{50.0f, 25.0f}, {200.0f, 200.0f}}) == UiRect{{50.0f, 25.0f}, {100.0f, 50.0f}});
    CHECK(intersect(a, {{10.0f, 10.0f}, {20.0f, 20.0f}}) == UiRect{{10.0f, 10.0f}, {20.0f, 20.0f}});
    const UiRect disjoint = intersect(a, {{150.0f, 0.0f}, {200.0f, 50.0f}});
    CHECK(disjoint.empty());
    CHECK(disjoint == UiRect{a.min, a.min});
    CHECK(intersect(a, {{100.0f, 0.0f}, {200.0f, 50.0f}}).empty()); // touching edges share no area
}

TEST_CASE("insetting a rectangle never inverts it")
{
    const UiRect rect{{10.0f, 20.0f}, {110.0f, 70.0f}};
    CHECK(inset(rect, {5.0f, 2.0f, 10.0f, 4.0f}) == UiRect{{15.0f, 22.0f}, {100.0f, 66.0f}});
    CHECK(inset(rect, {-5.0f, 0.0f, 0.0f, 0.0f}) == UiRect{{5.0f, 20.0f}, {110.0f, 70.0f}});
    const UiRect collapsed = inset(rect, {80.0f, 40.0f, 80.0f, 40.0f});
    CHECK(collapsed.empty());
    CHECK(collapsed.size().x >= 0.0f);
    CHECK(collapsed.size().y >= 0.0f);
}

TEST_CASE("rectangles contain their min corner but not their max corner")
{
    const UiRect rect{{0.0f, 0.0f}, {10.0f, 10.0f}};
    CHECK(rect.contains({0.0f, 0.0f}));
    CHECK(rect.contains({9.99f, 5.0f}));
    CHECK_FALSE(rect.contains({10.0f, 5.0f}));
    CHECK_FALSE(rect.contains({-0.01f, 5.0f}));
    CHECK(UiRect{}.empty());
}

TEST_CASE("placements anchor to their parent and stretch with different anchors")
{
    const UiRect parent{{100.0f, 50.0f}, {1100.0f, 550.0f}};
    UiPlacement topRight;
    topRight.anchorMin = topRight.anchorMax = {1.0f, 0.0f};
    topRight.offsetMin = {-320.0f, 24.0f};
    topRight.offsetMax = {-24.0f, 88.0f};
    CHECK(resolvePlacement(topRight, parent) == UiRect{{780.0f, 74.0f}, {1076.0f, 138.0f}});

    UiPlacement stretch;
    stretch.anchorMin = {0.0f, 0.5f};
    stretch.anchorMax = {1.0f, 1.0f};
    stretch.offsetMin = {10.0f, 0.0f};
    stretch.offsetMax = {-10.0f, -10.0f};
    CHECK(resolvePlacement(stretch, parent) == UiRect{{110.0f, 300.0f}, {1090.0f, 540.0f}});

    UiPlacement centred;
    centred.anchorMin = centred.anchorMax = {0.5f, 0.5f};
    centred.offsetMin = {-50.0f, -20.0f};
    centred.offsetMax = {50.0f, 20.0f};
    CHECK(resolvePlacement(centred, parent) == UiRect{{550.0f, 280.0f}, {650.0f, 320.0f}});
}

TEST_CASE("a placement whose max is before its min is clamped to empty")
{
    UiPlacement inverted;
    inverted.anchorMin = {1.0f, 1.0f};
    inverted.anchorMax = {0.0f, 0.0f};
    const UiRect rect = resolvePlacement(inverted, {{0.0f, 0.0f}, {100.0f, 100.0f}});
    CHECK(rect.min == Vec2(100.0f, 100.0f));
    CHECK(rect.max == Vec2(100.0f, 100.0f));
    UiPlacement offsets;
    offsets.offsetMin = {50.0f, 10.0f};
    offsets.offsetMax = {20.0f, 40.0f};
    CHECK(resolvePlacement(offsets, {{0.0f, 0.0f}, {100.0f, 100.0f}}) ==
          UiRect{{50.0f, 10.0f}, {50.0f, 40.0f}});
}

TEST_CASE("Fit keeps the reference size inside targets of any aspect ratio")
{
    const UiCanvasMetrics hd = metrics(UiScaleMode::Fit, {1920, 1080});
    CHECK(near(hd.pixelsPerUnit, 1.0f));
    CHECK(near(hd.size, {1920.0f, 1080.0f}));
    const UiCanvasMetrics uhd = metrics(UiScaleMode::Fit, {3840, 2160});
    CHECK(near(uhd.pixelsPerUnit, 2.0f));
    CHECK(near(uhd.size, {1920.0f, 1080.0f}));
    const UiCanvasMetrics ultrawide = metrics(UiScaleMode::Fit, {2560, 1080});
    CHECK(near(ultrawide.pixelsPerUnit, 1.0f));
    CHECK(near(ultrawide.size, {2560.0f, 1080.0f}));
    const UiCanvasMetrics classic = metrics(UiScaleMode::Fit, {1440, 1080});
    CHECK(near(classic.pixelsPerUnit, 0.75f));
    CHECK(near(classic.size, {1920.0f, 1440.0f}, 1e-2f));
    const UiCanvasMetrics portrait = metrics(UiScaleMode::Fit, {1080, 1920});
    CHECK(near(portrait.pixelsPerUnit, 0.5625f));
    CHECK(near(portrait.size, {1920.0f, 3413.3333f}, 1e-2f));
    CHECK(portrait.safeArea == UiRect{{0.0f, 0.0f}, portrait.size});
}

TEST_CASE("MatchHeight and MatchWidth fix one axis of the canvas")
{
    const UiCanvasMetrics ultrawideHeight = metrics(UiScaleMode::MatchHeight, {2560, 1080});
    CHECK(near(ultrawideHeight.pixelsPerUnit, 1.0f));
    CHECK(near(ultrawideHeight.size, {2560.0f, 1080.0f}));
    const UiCanvasMetrics portraitHeight = metrics(UiScaleMode::MatchHeight, {1080, 1920});
    CHECK(near(portraitHeight.size.y, 1080.0f, 1e-2f));
    CHECK(near(portraitHeight.size.x, 607.5f, 1e-2f));
    const UiCanvasMetrics classicHeight = metrics(UiScaleMode::MatchHeight, {1440, 1080});
    CHECK(near(classicHeight.size, {1440.0f, 1080.0f}, 1e-2f));

    const UiCanvasMetrics ultrawideWidth = metrics(UiScaleMode::MatchWidth, {2560, 1080});
    CHECK(near(ultrawideWidth.size, {1920.0f, 810.0f}, 1e-2f));
    const UiCanvasMetrics portraitWidth = metrics(UiScaleMode::MatchWidth, {1080, 1920});
    CHECK(near(portraitWidth.size, {1920.0f, 3413.3333f}, 1e-2f));
    const UiCanvasMetrics uhdWidth = metrics(UiScaleMode::MatchWidth, {3840, 2160});
    CHECK(near(uhdWidth.pixelsPerUnit, 2.0f));
}

TEST_CASE("Pixels maps one unit to one pixel times the user scale")
{
    CHECK(near(metrics(UiScaleMode::Pixels, {2560, 1080}).pixelsPerUnit, 1.0f));
    CHECK(near(metrics(UiScaleMode::Pixels, {2560, 1080}).size, {2560.0f, 1080.0f}));
    CHECK(near(metrics(UiScaleMode::Pixels, {1080, 1920}, 2.0f).size, {540.0f, 960.0f}));
}

TEST_CASE("the user scale multiplies every mode and is clamped to its range")
{
    CHECK(near(metrics(UiScaleMode::Fit, {1920, 1080}, 1.5f).pixelsPerUnit, 1.5f));
    CHECK(near(metrics(UiScaleMode::MatchWidth, {1920, 1080}, 2.0f).pixelsPerUnit, 2.0f));
    CHECK(near(metrics(UiScaleMode::Fit, {1920, 1080}, 10.0f).pixelsPerUnit, 3.0f));
    CHECK(near(metrics(UiScaleMode::Fit, {1920, 1080}, 0.1f).pixelsPerUnit, 0.5f));
    CHECK(near(metrics(UiScaleMode::Pixels, {1920, 1080}, 0.25f).pixelsPerUnit, 0.5f));
}

TEST_CASE("degenerate targets and scales give one pixel per unit")
{
    CHECK(near(metrics(UiScaleMode::Fit, {0, 1080}).pixelsPerUnit, 1.0f));
    CHECK(near(metrics(UiScaleMode::Fit, {0, 1080}).size, {0.0f, 1080.0f}));
    CHECK(near(metrics(UiScaleMode::MatchHeight, {1920, 0}).pixelsPerUnit, 1.0f));
    CHECK(near(metrics(UiScaleMode::Fit, {0, 0}).size, {0.0f, 0.0f}));
    for (const f32 userScale :
         {0.0f, -1.0f, std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity()})
    {
        CHECK(near(metrics(UiScaleMode::Fit, {3840, 2160}, userScale).pixelsPerUnit, 1.0f));
    }
    UiCanvasScale scale;
    scale.referenceSize = {0.0f, 1080.0f};
    CHECK(near(canvasMetrics(scale, {3840, 2160}).pixelsPerUnit, 1.0f));
    scale.referenceSize = {1920.0f, std::numeric_limits<f32>::infinity()};
    CHECK(near(canvasMetrics(scale, {3840, 2160}).pixelsPerUnit, 1.0f));
    scale.referenceSize = {-1920.0f, 1080.0f};
    CHECK(near(canvasMetrics(scale, {3840, 2160}).pixelsPerUnit, 1.0f));
}

TEST_CASE("safe insets in pixels become the safe area in canvas units")
{
    const UiCanvasMetrics canvas = canvasMetrics({}, {3840, 2160}, {100.0f, 50.0f, 200.0f, 0.0f});
    CHECK(near(canvas.pixelsPerUnit, 2.0f));
    CHECK(near(canvas.safeArea.min, {50.0f, 25.0f}));
    CHECK(near(canvas.safeArea.max, {1820.0f, 1080.0f}));

    const UiCanvasMetrics negative =
        canvasMetrics({}, {1920, 1080}, {-10.0f, std::numeric_limits<f32>::quiet_NaN(), 0.0f, 0.0f});
    CHECK(negative.safeArea == UiRect{{0.0f, 0.0f}, {1920.0f, 1080.0f}});

    const UiCanvasMetrics huge = canvasMetrics({}, {1920, 1080}, {2000.0f, 600.0f, 2000.0f, 600.0f});
    CHECK(huge.safeArea.empty());
    CHECK(huge.safeArea.size().x >= 0.0f);
    CHECK(huge.safeArea.size().y >= 0.0f);
}

TEST_CASE("UI transforms scale, then rotate clockwise on screen about their pivot")
{
    CHECK(makeUiTransform({10.0f, 20.0f}, 0.0f, {1.0f, 1.0f}) == UiTransform2D{});
    const UiTransform2D quarter =
        makeUiTransform({10.0f, 10.0f}, std::numbers::pi_v<f32> * 0.5f, {1.0f, 1.0f});
    CHECK(near(quarter.apply({20.0f, 10.0f}), {10.0f, 20.0f})); // right of the pivot turns to below it
    CHECK(near(quarter.apply({10.0f, 10.0f}), {10.0f, 10.0f}));
    const UiTransform2D doubled = makeUiTransform({10.0f, 10.0f}, 0.0f, {2.0f, 3.0f});
    CHECK(near(doubled.apply({20.0f, 20.0f}), {30.0f, 40.0f}));
    CHECK(near(doubled.apply({10.0f, 10.0f}), {10.0f, 10.0f}));
    const UiTransform2D both = makeUiTransform({0.0f, 0.0f}, std::numbers::pi_v<f32> * 0.5f, {2.0f, 1.0f});
    CHECK(near(both.apply({1.0f, 0.0f}), {0.0f, 2.0f}));
}

TEST_CASE("combined transforms apply the child first")
{
    UiTransform2D move;
    move.translation = {100.0f, 0.0f};
    const UiTransform2D grow = makeUiTransform({0.0f, 0.0f}, 0.0f, {2.0f, 2.0f});
    CHECK(near(combine(move, grow).apply({1.0f, 1.0f}), {102.0f, 2.0f}));
    CHECK(near(combine(grow, move).apply({1.0f, 1.0f}), {202.0f, 2.0f}));
    const UiTransform2D turn = makeUiTransform({5.0f, 5.0f}, 1.0f, {1.5f, 0.5f});
    CHECK(combine(UiTransform2D{}, turn) == turn);
    CHECK(combine(turn, UiTransform2D{}) == turn);
    const Vec2 point{3.0f, -7.0f};
    CHECK(near(combine(turn, grow).apply(point), turn.apply(grow.apply(point))));
}
