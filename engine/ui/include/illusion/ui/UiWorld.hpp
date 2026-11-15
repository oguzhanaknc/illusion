#pragma once

/// @file
/// The UI System (ADR 0057): lays out the UI Entities of a World and records them into a UiDrawList.
///
/// @code
/// illusion::registerUiComponents(); // once, with the other reflected Components
/// illusion::UiWorld ui(world);
/// ui.setAssets(&uiAssets); // fonts and images, from the renderer side
/// // each frame, after the game's update:
/// ui.update({targetSize});
/// uiRenderer->render(commands, target, ui.drawData());
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ui/UiDrawList.hpp>
#include <illusion/ui/UiGeometry.hpp>
#include <illusion/ui/UiWidgets.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

class World;

namespace text
{
class Localization;
}

/// Reflects the saved UI Components ("illusion.UiCanvas", "illusion.UiElement", "illusion.UiImage",
/// "illusion.UiText", "illusion.UiStack", "illusion.UiGrid", "illusion.UiInteractable", "illusion.UiButton",
/// "illusion.UiToggle", "illusion.UiSlider", "illusion.UiTextField", "illusion.UiScrollView",
/// "illusion.UiGlyph") and the runtime UiComputedRect, UiInteraction and UiTextArguments. Safe to call more
/// than once.
void registerUiComponents();

/// What @p value becomes on @p slider: snapped to its step from min and kept between min and max, as input
/// does; min (or the lower end) for a value that is not finite.
[[nodiscard]] f32 snapSliderValue(const UiSlider& slider, f32 value) noexcept;

/// An image the UI can draw.
struct UiImageSource
{
    UiTextureId texture{}; ///< The image in the UiRenderer; none while it loads or when it failed.
    Vec2 size{0.0f};       ///< Its size in pixels, for nine-slice borders and aspect.
};

/// Where a UiWorld gets fonts and images from, implemented next to the renderer (UiRenderAssets) or by a
/// test. Both functions are called every frame for every visible element: they must not allocate once an
/// asset is resolved, and return nothing while it loads.
class UiAssets : NonCopyable
{
public:
    /// The font of @p font; a null `font` member while it loads, failed, or for no asset.
    [[nodiscard]] virtual UiFontFace font(const AssetRef& font) = 0;
    /// The image of @p image.
    [[nodiscard]] virtual UiImageSource image(const AssetRef& image) = 0;

protected:
    UiAssets() = default;
    virtual ~UiAssets() = default;
};

/// Where a UiWorld gets the labels of UiGlyph from, implemented over the input actions by the session or by
/// a test.
class UiGlyphSource : NonCopyable
{
public:
    /// The label of the first binding of @p action for the device used last, in the active language; empty
    /// for an unknown action or one without a binding for that device. Valid until the next call.
    [[nodiscard]] virtual std::string_view actionLabel(std::string_view action) = 0;
    /// Changes whenever a label may have changed (the device used last, the bindings, the language); the
    /// UiWorld asks for labels again only then.
    [[nodiscard]] virtual u64 version() const noexcept = 0;

protected:
    UiGlyphSource() = default;
    virtual ~UiGlyphSource() = default;
};

/// The target a UiWorld lays out for.
struct UiFrame
{
    UVec2 targetPixels{0};     ///< Size of the colour target.
    UiEdges safeInsetPixels{}; ///< Unsafe borders of the display, in pixels.
};

/// Creation parameters of a UiWorld.
struct UiWorldDesc
{
    UiDrawListDesc drawList{}; ///< Capacities of the frame's draw list.
    u32 maxElements = 16384;   ///< UI Entities laid out per frame; the rest are skipped and counted.
    u32 maxDepth = 64;         ///< Nesting below a canvas; deeper Entities are skipped and counted.
    u32 maxEvents = 256;       ///< Events kept per update; the rest are dropped and counted.
    /// Never write saved Components (the editor's preview of the World it edits): a scroll view's offset
    /// beyond its content is clamped for the layout only instead of in its UiScrollView. Runtime Components
    /// (UiComputedRect, UiInteraction) are still written.
    bool preview = false;
};

/// A canvas of the last layout (UiWorld::layout()).
struct UiLayoutCanvas
{
    Entity entity{};             ///< The Entity with UiCanvas.
    i32 order = 0;               ///< UiCanvas::order.
    UiCanvasMetrics metrics{};   ///< Its scale and size on the frame's target.
    UiRect area{};               ///< The rectangle its children are placed in (the safe area or all of it).
    bool modal = false;          ///< UiCanvas::modal.
    bool pointerEligible = true; ///< The pointer and the focus can reach it (no modal canvas above it).
};

/// An element of the last layout (UiWorld::layout()).
struct UiLayoutElement
{
    static constexpr u32 kNoParent = ~0u; ///< parent of a child of the canvas.

    Entity entity{};           ///< The Entity with UiElement.
    u32 canvas = 0;            ///< Index of its canvas in UiLayout::canvases.
    u32 parent = kNoParent;    ///< Index of its parent element in UiLayout::elements.
    UiRect rect{};             ///< Its rectangle in canvas units, before its transform.
    UiTransform2D transform{}; ///< Accumulated rotation and scale.
    f32 opacity = 1.0f;        ///< Accumulated opacity.
    bool visible = true;       ///< Drawn: some opacity, and no toggle that is off hides it or an ancestor.
    bool clipsChildren =
        false;                 ///< Its descendants are clipped to it (UiElement::clipChildren, scroll views).
    bool interactable = false; ///< Has UiInteractable.
};

/// The last layout of a UiWorld: canvases in draw order and their elements in draw order (each after its
/// parent, later siblings after earlier ones).
struct UiLayout
{
    std::vector<UiLayoutCanvas> canvases{};  ///< Active canvases, bottom first.
    std::vector<UiLayoutElement> elements{}; ///< Laid-out elements.
};

/// Counters of the last UiWorld::update().
struct UiWorldStatistics
{
    u32 canvases = 0;          ///< Active canvases drawn.
    u32 elements = 0;          ///< Elements laid out.
    u32 skippedElements = 0;   ///< Elements beyond maxElements or maxDepth.
    u32 droppedPrimitives = 0; ///< Primitives that did not fit the draw list.
    u32 missingGlyphs = 0;     ///< Code points drawn with a fallback glyph.
    u32 missingKeys = 0;       ///< UiText keys the Localization does not have (the key is shown).
    u32 droppedEvents = 0;     ///< Events beyond UiWorldDesc::maxEvents.
    bool layoutReused = false; ///< Nothing changed since the last update: the rectangles were kept.
};

/// Lays out and draws the UI of one World. Each update(): every active Entity with UiCanvas is a root on the
/// target (canvases in ascending `order`, ties by Entity index); its descendants with UiElement are placed
/// by their placement, or by a parent's UiStack or UiGrid, in hierarchy order; UiComputedRect is written to
/// each; then images and texts are recorded into the draw list, children over parents, later siblings over
/// earlier ones. Entities with the Inactive tag, and their subtrees, are skipped. A steady update does not
/// allocate. Not thread-safe; use on the thread that owns the World.
///
/// With UiInput, elements with UiInteractable react (UiWidgets.hpp): the pointer hits the topmost enabled
/// interactable element under it (later canvases and later nodes on top, through each element's transform
/// and inside the clips of its ancestors; elements with no opacity are not hit), navigation moves the focus,
/// and the frame's UiEvents are in events(). While a canvas with UiCanvas::modal shows an element, only it
/// and the canvases above it can be hit or focused. A UiToggle that is off hides its first child; a UiSlider
/// places its first child from its start to the value (the fill) and centres its second child on the value
/// along its axis (the handle keeps the size and cross-axis position of its placement).
///
/// A focused UiTextField takes UiInput::text and the caret keys; navigation left and right moves its caret,
/// up and down move the focus. The caret is run-time state of the UiWorld: it goes to the end when the field
/// gets the focus or its text is set from outside. A text wider than the field scrolls so that the caret
/// stays visible, clipped to the element.
///
/// A UiScrollView clips its children and moves its first child by its offset. Its descendants that lie
/// wholly outside the view are neither hit nor recorded, so the draw cost of a long list is bounded by what
/// is visible; their Entities still exist and are laid out every time the layout runs (there is no
/// recycling of rows).
///
/// A UiGlyph replaces the text its UiText shows with the label from setGlyphs(), without writing UiText.
class UiWorld : NonCopyable
{
public:
    /// Works on @p world, which must outlive it. Call registerUiComponents() first.
    explicit UiWorld(World& world, const UiWorldDesc& desc = {});
    ~UiWorld();

    /// Fonts and images; null (the default) draws no text and no images, only solid colours.
    void setAssets(UiAssets* assets) noexcept;
    /// The string tables of UiText::key, formatted with the element's UiTextArguments when it has them;
    /// null (the default) shows the key itself.
    void setLocalization(const text::Localization* localization) noexcept;
    /// The labels of UiGlyph; null (the default) leaves their UiText as it is.
    void setGlyphs(UiGlyphSource* glyphs) noexcept;

    /// Lays out again at the next update even if no version changed: for code that wrote UI Components
    /// through references without `world.markChanged<T>()`.
    void invalidateLayout() noexcept;

    /// Lays out for @p frame and records the draw list. The layout is recomputed only when a UI Component,
    /// the hierarchy, the Inactive tags, the locale or the frame changed since the last update (code that
    /// writes a UI Component through a reference calls `world.markChanged<T>()`, as for Hierarchy); the draw
    /// list is recorded every time (assets may have finished loading).
    void update(const UiFrame& frame);

    /// update(frame) that also handles @p input against the rectangles of this layout before it records:
    /// hover, press and click, focus and navigation (repeating while held), toggle and slider values, which
    /// it writes to the Components (marked changed) and shows in the same update.
    void update(const UiFrame& frame, const UiInput& input);

    /// Events of the last update, in the order they happened.
    [[nodiscard]] std::span<const UiEvent> events() const noexcept;

    /// The element with the focus of navigation, or null.
    [[nodiscard]] Entity focused() const noexcept;

    /// Gives @p entity the focus (no event); null clears it. Ignored for an element that the last update did
    /// not lay out on a canvas that can be focused, or that is not enabled and focusable.
    void setFocus(Entity entity);

    /// True when the last update laid out at least one element on an active canvas with UiCanvas::modal.
    [[nodiscard]] bool modalActive() const noexcept;

    /// Replaces @p out with the canvases and elements of the last update (allocates; for tools).
    void layout(UiLayout& out) const;

    /// The topmost element the pointer would hit at @p pixels of the last update's target: enabled,
    /// interactable, visible, inside its ancestors' clips and on a canvas the pointer can reach; null for
    /// none.
    [[nodiscard]] Entity hitTest(Vec2 pixels);

    /// What the last update() recorded; valid until the next one.
    [[nodiscard]] UiDrawData drawData() const noexcept;
    /// Counters of the last update().
    [[nodiscard]] const UiWorldStatistics& statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
