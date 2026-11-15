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

namespace illusion
{

class World;

namespace text
{
class Localization;
}

/// Reflects the saved UI Components ("illusion.UiCanvas", "illusion.UiElement", "illusion.UiImage",
/// "illusion.UiText", "illusion.UiStack", "illusion.UiGrid", "illusion.UiInteractable", "illusion.UiButton",
/// "illusion.UiToggle", "illusion.UiSlider") and the runtime UiComputedRect and UiInteraction. Safe to call
/// more than once.
void registerUiComponents();

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
class UiWorld : NonCopyable
{
public:
    /// Works on @p world, which must outlive it. Call registerUiComponents() first.
    explicit UiWorld(World& world, const UiWorldDesc& desc = {});
    ~UiWorld();

    /// Fonts and images; null (the default) draws no text and no images, only solid colours.
    void setAssets(UiAssets* assets) noexcept;
    /// The string tables of UiText::key; null (the default) shows the key itself.
    void setLocalization(const text::Localization* localization) noexcept;

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

    /// What the last update() recorded; valid until the next one.
    [[nodiscard]] UiDrawData drawData() const noexcept;
    /// Counters of the last update().
    [[nodiscard]] const UiWorldStatistics& statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
