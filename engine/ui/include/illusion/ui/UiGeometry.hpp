#pragma once

/// @file
/// Geometry of the game UI (ADR 0057): rectangles in canvas units (y down, origin at the top left),
/// placement by anchors and offsets, and the scale of a canvas on a target of any size.
///
/// @code
/// const illusion::UiCanvasMetrics canvas = illusion::canvasMetrics({}, {2560, 1440});
/// illusion::UiPlacement topRight;
/// topRight.anchorMin = topRight.anchorMax = {1.0f, 0.0f};
/// topRight.offsetMin = {-320.0f, 24.0f};
/// topRight.offsetMax = {-24.0f, 88.0f};
/// const illusion::UiRect rect = illusion::resolvePlacement(topRight, canvas.safeArea);
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// Axis-aligned rectangle in canvas units; `min` is the top left corner.
struct UiRect
{
    Vec2 min{0.0f}; ///< Top left corner.
    Vec2 max{0.0f}; ///< Bottom right corner.

    /// Width and height; negative when max is before min.
    [[nodiscard]] Vec2 size() const noexcept { return max - min; }
    /// True when the rectangle covers no area.
    [[nodiscard]] bool empty() const noexcept { return max.x <= min.x || max.y <= min.y; }
    /// True when @p point is inside (min inclusive, max exclusive).
    [[nodiscard]] bool contains(Vec2 point) const noexcept
    {
        return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    }
    /// Rectangles are equal when their corners are.
    friend bool operator==(const UiRect&, const UiRect&) noexcept = default;
};

/// Distances from the four edges of a rectangle: padding, nine-slice borders, safe insets.
struct UiEdges
{
    f32 left = 0.0f;   ///< From the left edge.
    f32 top = 0.0f;    ///< From the top edge.
    f32 right = 0.0f;  ///< From the right edge.
    f32 bottom = 0.0f; ///< From the bottom edge.

    /// Edges are equal when the four distances are.
    friend bool operator==(const UiEdges&, const UiEdges&) noexcept = default;
};

/// The part two rectangles share; an empty rectangle at a.min when they do not overlap.
[[nodiscard]] UiRect intersect(const UiRect& a, const UiRect& b) noexcept;

/// @p rect made smaller by @p edges on each side, never smaller than empty.
[[nodiscard]] UiRect inset(const UiRect& rect, const UiEdges& edges) noexcept;

/// Where an element is inside its parent: two anchors, normalised to the parent (0 = left/top, 1 =
/// right/bottom), and two offsets in canvas units. Equal anchors give a fixed size at a point of the
/// parent; different anchors stretch with it.
struct UiPlacement
{
    Vec2 anchorMin{0.0f}; ///< Anchor of the top left corner.
    Vec2 anchorMax{0.0f}; ///< Anchor of the bottom right corner.
    Vec2 offsetMin{0.0f}; ///< Top left corner relative to its anchor, in canvas units.
    Vec2 offsetMax{0.0f}; ///< Bottom right corner relative to its anchor, in canvas units.

    /// Placements are equal when anchors and offsets are.
    friend bool operator==(const UiPlacement&, const UiPlacement&) noexcept = default;
};

/// The rectangle of @p placement inside @p parent: `parent.min + anchor * parent.size() + offset` for each
/// corner; max is clamped to min, so the result is never inverted.
[[nodiscard]] UiRect resolvePlacement(const UiPlacement& placement, const UiRect& parent) noexcept;

/// How canvas units map to the pixels of the target.
enum class UiScaleMode : u8
{
    Fit,         ///< The reference size fits inside the target: the smaller of the two axis ratios.
    MatchHeight, ///< The canvas is always the reference height tall; its width follows the aspect.
    MatchWidth,  ///< The canvas is always the reference width wide; its height follows the aspect.
    Pixels,      ///< One unit is one pixel (times userScale).
};

/// Scale settings of a canvas.
struct UiCanvasScale
{
    UiScaleMode mode = UiScaleMode::Fit;  ///< How the reference size maps to the target.
    Vec2 referenceSize{1920.0f, 1080.0f}; ///< The size the UI was designed for, in canvas units.
    f32 userScale = 1.0f;                 ///< Extra factor chosen by the player (accessibility), 0.5 to 3.

    /// Scales are equal when every field is.
    friend bool operator==(const UiCanvasScale&, const UiCanvasScale&) noexcept = default;
};

/// A canvas on a target: the result of canvasMetrics().
struct UiCanvasMetrics
{
    f32 pixelsPerUnit = 1.0f; ///< Target pixels per canvas unit.
    Vec2 size{0.0f};          ///< The whole target in canvas units.
    UiRect safeArea{};        ///< The part of the canvas inside the display's safe insets.
};

/// The metrics of a canvas with @p scale on a target of @p targetPixels whose unsafe borders are
/// @p safeInsetPixels wide. A target with a zero side, or a reference size or user scale that is not
/// positive and finite, gives one pixel per unit; userScale is clamped to its range.
[[nodiscard]] UiCanvasMetrics canvasMetrics(const UiCanvasScale& scale, UVec2 targetPixels,
                                            const UiEdges& safeInsetPixels = {}) noexcept;

/// A 2D affine transform: `x * p.x + y * p.y + translation`.
struct UiTransform2D
{
    Vec2 x{1.0f, 0.0f};     ///< Image of the unit x vector.
    Vec2 y{0.0f, 1.0f};     ///< Image of the unit y vector.
    Vec2 translation{0.0f}; ///< Added after the linear part.

    /// The transformed @p point.
    [[nodiscard]] Vec2 apply(Vec2 point) const noexcept { return x * point.x + y * point.y + translation; }
    /// Transforms are equal when every field is.
    friend bool operator==(const UiTransform2D&, const UiTransform2D&) noexcept = default;
};

/// The transform that scales by @p scale and then rotates by @p radians (clockwise on screen) about
/// @p pivot.
[[nodiscard]] UiTransform2D makeUiTransform(Vec2 pivot, f32 radians, Vec2 scale) noexcept;

/// The transform that applies @p child first, then @p parent.
[[nodiscard]] UiTransform2D combine(const UiTransform2D& parent, const UiTransform2D& child) noexcept;

} // namespace illusion
