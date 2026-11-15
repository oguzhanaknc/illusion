#pragma once

/// @file
/// The saved Components of the game UI (ADR 0057). A UI is a subtree of Entities: a root with UiCanvas and
/// descendants with UiElement, drawn in hierarchy order. They have no Transform.
///
/// @code
/// const illusion::Entity canvas = world.create(illusion::UiCanvas{});
/// illusion::UiElement placement;
/// placement.placement.offsetMin = {24.0f, 24.0f};
/// placement.placement.offsetMax = {344.0f, 88.0f};
/// illusion::UiText text;
/// text.text = "Score 0";
/// const illusion::Entity score = world.create(placement, std::move(text));
/// illusion::setParent(world, score, canvas);
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ui/UiDrawList.hpp>
#include <illusion/ui/UiGeometry.hpp>

#include <array>
#include <string>

namespace illusion
{

/// Root of a UI: its descendants with UiElement are laid out on the target and drawn over the scene
/// (reflected as "illusion.UiCanvas").
struct UiCanvas
{
    UiCanvasScale scale{};   ///< How canvas units map to the target.
    i32 order = 0;           ///< Canvases with a higher order are drawn on top.
    bool useSafeArea = true; ///< Lay out inside the display's safe insets instead of the whole target.
    /// While shown, the game's other input actions see no input (a pause menu).
    bool modal = false;
};

/// Places an Entity inside its parent's rectangle (reflected as "illusion.UiElement"). A child of a UiStack
/// or UiGrid is placed by that parent instead and uses only `size`.
struct UiElement
{
    UiPlacement placement{}; ///< Anchors and offsets in the parent.
    /// Size in canvas units inside a UiStack or UiGrid; a zero axis takes the content's size (UiText,
    /// UiImage) or stretches.
    Vec2 size{0.0f};
    Vec2 pivot{0.5f};          ///< Centre of rotation and scale, normalised to the element's rectangle.
    f32 rotation = 0.0f;       ///< Clockwise, in radians; applies to the descendants too.
    Vec2 scale{1.0f};          ///< Visual scale about the pivot; does not affect layout.
    f32 opacity = 1.0f;        ///< Multiplies the alpha of the element and its descendants.
    bool clipChildren = false; ///< Descendants are not drawn outside the element's rectangle.
};

/// Draws an image, or a solid colour without one, over the element (reflected as "illusion.UiImage").
struct UiImage
{
    AssetRef image{}; ///< The image; none draws the tint as a solid colour.
    Vec4 tint{1.0f};  ///< Linear RGBA multiplied with the image.
    /// Nine-slice borders in pixels of the image; all zero stretches the whole image.
    UiEdges border{};
    f32 borderScale = 1.0f;      ///< Canvas units per image pixel of the borders.
    bool preserveAspect = false; ///< Fit the image inside the element keeping its aspect (no borders).
};

/// Vertical placement of a text in its element.
enum class UiVerticalAlign : u8
{
    Top,    ///< First line at the top.
    Middle, ///< Lines centred.
    Bottom, ///< Last line at the bottom.
};

/// Draws text in the element (reflected as "illusion.UiText").
struct UiText
{
    std::string text; ///< The text, UTF-8; used when key is empty.
    /// Name of a string table key of the game's Localization; its message is shown instead of text.
    std::string key;
    AssetRef font{};                                      ///< The font file.
    f32 size = 24.0f;                                     ///< Em size in canvas units.
    Vec4 color{1.0f};                                     ///< Linear RGBA.
    UiTextAlign align = UiTextAlign::Start;               ///< Horizontal placement.
    UiVerticalAlign verticalAlign = UiVerticalAlign::Top; ///< Vertical placement.
    bool wrap = true;                                     ///< Wrap lines at the element's width.
    f32 lineSpacing = 1.0f;                               ///< Multiplier of the font's line height.
    f32 letterSpacing = 0.0f;                             ///< Extra advance per glyph in ems.
};

/// Direction of a UiStack.
enum class UiAxis : u8
{
    Horizontal, ///< Children left to right.
    Vertical,   ///< Children top to bottom.
};

/// Placement of children across the direction of a UiStack.
enum class UiCrossAlign : u8
{
    Start,   ///< At the left (vertical stack) or top (horizontal stack).
    Center,  ///< Centred.
    End,     ///< At the right or bottom.
    Stretch, ///< Filling the stack's inner size.
};

/// Places the element's children one after another (reflected as "illusion.UiStack").
struct UiStack
{
    UiAxis axis = UiAxis::Vertical;                  ///< Direction.
    f32 spacing = 0.0f;                              ///< Gap between children in canvas units.
    UiEdges padding{};                               ///< Space inside the element's edges.
    UiCrossAlign crossAlign = UiCrossAlign::Stretch; ///< Placement across the direction.
};

/// Places the element's children in rows of equal cells (reflected as "illusion.UiGrid").
struct UiGrid
{
    u32 columns = 2;       ///< Cells per row, at least 1.
    Vec2 cellSize{100.0f}; ///< Size of each cell in canvas units.
    Vec2 spacing{0.0f};    ///< Gaps between columns and rows.
    UiEdges padding{};     ///< Space inside the element's edges.
};

/// One named argument of UiTextArguments, stored inline.
struct UiTextArgumentValue
{
    std::array<char, 32> name{};  ///< The argument's name, not null-terminated.
    std::array<char, 256> text{}; ///< Its text value, not null-terminated.
    f64 value = 0.0;              ///< Its number value.
    u8 nameLength = 0;            ///< Bytes of name used.
    u8 textLength = 0;            ///< Bytes of text used.
    bool isNumber = false;        ///< Which of the two values is used.
};

/// The arguments the UiText::key of an element is formatted with (runtime only, set by
/// UiService::setTextKey()); write it with markChanged() so that the layout follows.
struct UiTextArguments
{
    static constexpr u32 kMaxArguments = 8;   ///< Arguments per text.
    static constexpr u32 kMaxNameBytes = 31;  ///< Longest argument name.
    static constexpr u32 kMaxTextBytes = 255; ///< Longest text value.

    std::array<UiTextArgumentValue, kMaxArguments> arguments{}; ///< The first count are used.
    u32 count = 0;                                              ///< Arguments used.
};

/// Where the layout put an element this frame (runtime only, written by the UI System).
struct UiComputedRect
{
    UiRect rect{};             ///< The element's rectangle in canvas units, before its transform.
    UiTransform2D transform{}; ///< Accumulated rotation and scale of the element and its ancestors.
    f32 opacity = 1.0f;        ///< Accumulated opacity.
};

} // namespace illusion
