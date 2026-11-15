#pragma once

/// @file
/// GPU-free math of canvas editing (ADR 0057, addendum UI-3d): anchor presets, placements that keep or reach
/// a rectangle, picking in a layout, and the handles the Game view drags. Everything is in canvas units, y
/// down, as UiLayout reports it.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ui/UiGeometry.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <array>
#include <optional>

namespace illusion::editor
{

/// The sixteen usual anchor settings: a point of the parent, a stretch along one axis at a line of the
/// other, or a stretch along both.
enum class UiAnchorPreset : u8
{
    TopLeft,       ///< Anchored to the parent's top left corner.
    Top,           ///< To the middle of its top edge.
    TopRight,      ///< To its top right corner.
    Left,          ///< To the middle of its left edge.
    Center,        ///< To its centre.
    Right,         ///< To the middle of its right edge.
    BottomLeft,    ///< To its bottom left corner.
    Bottom,        ///< To the middle of its bottom edge.
    BottomRight,   ///< To its bottom right corner.
    StretchTop,    ///< Stretches horizontally along the top edge.
    StretchMiddle, ///< Stretches horizontally along the vertical middle.
    StretchBottom, ///< Stretches horizontally along the bottom edge.
    StretchLeft,   ///< Stretches vertically along the left edge.
    StretchCenter, ///< Stretches vertically along the horizontal middle.
    StretchRight,  ///< Stretches vertically along the right edge.
    Stretch,       ///< Stretches along both axes.
};

/// Number of UiAnchorPreset values.
inline constexpr u32 kUiAnchorPresetCount = 16;

/// A pair of anchors, normalised to the parent's rectangle.
struct UiAnchors
{
    Vec2 min{0.0f}; ///< Anchor of the top left corner.
    Vec2 max{0.0f}; ///< Anchor of the bottom right corner.
};

/// The anchors of @p preset.
[[nodiscard]] UiAnchors anchorsOf(UiAnchorPreset preset) noexcept;

/// The preset whose anchors @p placement has exactly; none for any other anchors.
[[nodiscard]] std::optional<UiAnchorPreset> anchorPresetOf(const UiPlacement& placement) noexcept;

/// @p placement with the anchors @p anchors (each kept between 0 and 1, max not below min). With
/// @p keepRect the offsets change so that it resolves to the same rectangle in @p parent; without, the
/// offsets stay and the element moves with its anchors.
[[nodiscard]] UiPlacement withAnchors(const UiPlacement& placement, const UiRect& parent, UiAnchors anchors,
                                      bool keepRect) noexcept;

/// @p placement with the offsets that make it resolve to @p rect in @p parent; the anchors stay. A @p rect
/// whose max is below its min counts as empty at its min.
[[nodiscard]] UiPlacement placementFor(const UiPlacement& placement, const UiRect& parent,
                                       const UiRect& rect) noexcept;

/// The topmost element of @p layout at @p point: the last one in draw order that is visible, contains the
/// point after its transform and whose clipping ancestors contain it too. Interactable or not; null for
/// none.
[[nodiscard]] Entity pickUiElement(const UiLayout& layout, Vec2 point) noexcept;

/// What a pointer grabs on a selected element.
enum class UiHandle : u8
{
    None,        ///< Nothing.
    Body,        ///< The inside of the rectangle: moves it.
    Left,        ///< The left edge.
    TopLeft,     ///< The top left corner.
    Top,         ///< The top edge.
    TopRight,    ///< The top right corner.
    Right,       ///< The right edge.
    BottomRight, ///< The bottom right corner.
    Bottom,      ///< The bottom edge.
    BottomLeft,  ///< The bottom left corner.
    AnchorMin,   ///< The marker of the top left anchor.
    AnchorMax,   ///< The marker of the bottom right anchor.
    Anchors,     ///< Both markers, while they are at one point.
};

/// What the handles of one element are computed from.
struct UiHandleFrame
{
    UiRect rect{};             ///< The element's rectangle before its transform (UiLayoutElement::rect).
    UiTransform2D transform{}; ///< Its accumulated transform (UiLayoutElement::transform).
    UiRect parent{};           ///< The rectangle it is placed in: its parent's, or the canvas's area.
    UiTransform2D parentTransform{}; ///< The parent's accumulated transform; identity under a canvas.
    UiPlacement placement{};         ///< UiElement::placement.
    Vec2 size{0.0f};                 ///< UiElement::size.
    /// False for a child of a UiStack or UiGrid: the layout places it, so it has no Body and no anchor
    /// handles, and resizing changes UiElement::size.
    bool placed = true;
};

/// The frame of @p element of @p layout, with @p placement, @p size and @p placed from its Components; none
/// when the index is out of range.
[[nodiscard]] std::optional<UiHandleFrame> handleFrameOf(const UiLayout& layout, u32 element,
                                                         const UiPlacement& placement, Vec2 size,
                                                         bool placed) noexcept;

/// The corners of the frame's rectangle after its transform: top left, top right, bottom right, bottom left.
[[nodiscard]] std::array<Vec2, 4> handleCorners(const UiHandleFrame& frame) noexcept;

/// The positions of the two anchor markers after the parent's transform.
[[nodiscard]] UiAnchors anchorMarkers(const UiHandleFrame& frame) noexcept;

/// The handle of @p frame within @p radius of @p point: anchor markers before corners, corners before
/// edges, edges before the body. A frame that is not placed has neither Body nor anchor handles.
[[nodiscard]] UiHandle uiHandleHitTest(const UiHandleFrame& frame, Vec2 point, f32 radius) noexcept;

/// What a drag of a handle changes.
struct UiHandleEdit
{
    UiPlacement placement{}; ///< The placement to write.
    Vec2 size{0.0f};         ///< The UiElement::size to write.
};

/// A drag of one handle. The pointer's movement is read in the space the handle lives in (the parent's for
/// Body and the anchors, the element's own for edges and corners), so rotated and scaled elements follow
/// the pointer.
class UiHandleDrag
{
public:
    /// Starts dragging @p handle of @p frame with the pointer at @p point. False (and no drag) for None, for
    /// a handle the frame does not have, or when a transform cannot be inverted.
    [[nodiscard]] bool begin(const UiHandleFrame& frame, UiHandle handle, Vec2 point) noexcept;

    /// The edit for the pointer at @p point. Moved edges and offsets are rounded to multiples of @p snap
    /// canvas units (0 for none); an edge never crosses the opposite one; anchors stay inside the parent and
    /// keep the rectangle. Without begin() it returns the frame's own values.
    [[nodiscard]] UiHandleEdit update(Vec2 point, f32 snap) const noexcept;

    /// The handle being dragged; None without begin().
    [[nodiscard]] UiHandle handle() const noexcept { return m_handle; }

private:
    UiHandleFrame m_frame{};
    UiHandle m_handle = UiHandle::None;
    Vec2 m_start{0.0f}; // the pointer at begin(), in the handle's space
};

} // namespace illusion::editor
