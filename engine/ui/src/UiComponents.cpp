#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <array>
#include <numbers>

template <>
struct illusion::EnumReflection<illusion::UiScaleMode>
{
    static constexpr std::array kValues{illusion::EnumValue{"Fit", 0}, illusion::EnumValue{"MatchHeight", 1},
                                        illusion::EnumValue{"MatchWidth", 2},
                                        illusion::EnumValue{"Pixels", 3}};
};

template <>
struct illusion::EnumReflection<illusion::UiTextAlign>
{
    static constexpr std::array kValues{illusion::EnumValue{"Start", 0}, illusion::EnumValue{"Center", 1},
                                        illusion::EnumValue{"End", 2}};
};

template <>
struct illusion::EnumReflection<illusion::UiVerticalAlign>
{
    static constexpr std::array kValues{illusion::EnumValue{"Top", 0}, illusion::EnumValue{"Middle", 1},
                                        illusion::EnumValue{"Bottom", 2}};
};

template <>
struct illusion::EnumReflection<illusion::UiAxis>
{
    static constexpr std::array kValues{illusion::EnumValue{"Horizontal", 0},
                                        illusion::EnumValue{"Vertical", 1}};
};

template <>
struct illusion::EnumReflection<illusion::UiCrossAlign>
{
    static constexpr std::array kValues{illusion::EnumValue{"Start", 0}, illusion::EnumValue{"Center", 1},
                                        illusion::EnumValue{"End", 2}, illusion::EnumValue{"Stretch", 3}};
};

namespace illusion
{
namespace
{

constexpr std::string_view kRequiresElement[] = {"illusion.UiElement"};
constexpr std::string_view kRequiresInteractable[] = {"illusion.UiInteractable"};
constexpr std::string_view kDocsInteraction = "manual/ui.md#buttons-toggles-and-sliders";
constexpr std::string_view kDocsCanvas = "manual/ui.md#canvases-and-scaling";
constexpr std::string_view kDocsPlacement = "manual/ui.md#placement";
constexpr std::string_view kDocsImage = "manual/ui.md#images";
constexpr std::string_view kDocsText = "manual/ui.md#text";
constexpr std::string_view kDocsLayout = "manual/ui.md#stacks-and-grids";

} // namespace

void registerUiComponents()
{
    ComponentRegistry& registry = ComponentRegistry::instance();
    StructRegistry& structs = registry.structs();

    reflectStruct<UiEdges>("illusion.UiEdges", 1, structs)
        .meta({.displayName = "Edges", .docsAnchor = kDocsPlacement})
        .field("left", &UiEdges::left)
        .field("top", &UiEdges::top)
        .field("right", &UiEdges::right)
        .field("bottom", &UiEdges::bottom);
    reflectStruct<UiPlacement>("illusion.UiPlacement", 1, structs)
        .meta({.displayName = "Placement", .docsAnchor = kDocsPlacement})
        .field("anchorMin", &UiPlacement::anchorMin,
               {.description = "Anchor of the top left corner in the parent, 0 to 1.",
                .softMin = 0.0,
                .softMax = 1.0})
        .field("anchorMax", &UiPlacement::anchorMax,
               {.description = "Anchor of the bottom right corner in the parent, 0 to 1.",
                .softMin = 0.0,
                .softMax = 1.0})
        .field("offsetMin", &UiPlacement::offsetMin,
               {.description = "Top left corner relative to its anchor, in canvas units."})
        .field("offsetMax", &UiPlacement::offsetMax,
               {.description = "Bottom right corner relative to its anchor, in canvas units."});
    reflectStruct<UiCanvasScale>("illusion.UiCanvasScale", 1, structs)
        .meta({.displayName = "Canvas scale", .docsAnchor = kDocsCanvas})
        .field("mode", &UiCanvasScale::mode)
        .field("referenceSize", &UiCanvasScale::referenceSize,
               {.description = "The size the UI was designed for, in canvas units.", .min = 1.0})
        .field("userScale", &UiCanvasScale::userScale,
               {.description = "Extra factor chosen by the player.",
                .min = 0.5,
                .max = 3.0,
                .hint = FieldHint::Slider});

    reflect<UiCanvas>("illusion.UiCanvas", 1, registry)
        .meta({.displayName = "UI canvas",
               .category = "UI",
               .icon = "dashboard",
               .description = "Root of a game UI drawn over the scene.",
               .docsAnchor = kDocsCanvas})
        .field("scale", &UiCanvas::scale)
        .field("order", &UiCanvas::order, {.description = "Canvases with a higher order are drawn on top."})
        .field("useSafeArea", &UiCanvas::useSafeArea,
               {.description = "Lay out inside the display's safe area."})
        .field("modal", &UiCanvas::modal,
               {.description = "While shown, the game's other input actions see no input."});
    reflect<UiElement>("illusion.UiElement", 1, registry)
        .meta({.displayName = "UI element",
               .category = "UI",
               .icon = "crop_free",
               .description = "Places an Entity of a UI inside its parent.",
               .docsAnchor = kDocsPlacement})
        .field("placement", &UiElement::placement)
        .field("size", &UiElement::size,
               {.description = "Size inside a stack or grid; 0 takes the content's size or stretches.",
                .min = 0.0})
        .field("pivot", &UiElement::pivot,
               {.description = "Centre of rotation and scale, 0 to 1 in the element.",
                .softMin = 0.0,
                .softMax = 1.0})
        .field("rotation", &UiElement::rotation,
               {.description = "Clockwise; turns the descendants too.",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .softMin = -std::numbers::pi,
                .softMax = std::numbers::pi,
                .hint = FieldHint::Angle})
        .field("scale", &UiElement::scale,
               {.description = "Visual scale about the pivot; does not affect layout."})
        .field("opacity", &UiElement::opacity,
               {.description = "Multiplies the alpha of the element and its descendants.",
                .min = 0.0,
                .max = 1.0,
                .hint = FieldHint::Slider})
        .field("clipChildren", &UiElement::clipChildren,
               {.description = "Descendants are not drawn outside the element."});
    reflect<UiImage>("illusion.UiImage", 1, registry)
        .meta({.displayName = "UI image",
               .category = "UI",
               .icon = "image",
               .description = "Draws an image, or a solid colour without one.",
               .docsAnchor = kDocsImage,
               .requiredComponents = kRequiresElement})
        .field("image", &UiImage::image,
               {.description = "None draws the tint as a solid colour.", .assetKind = AssetKind::Texture})
        .field("tint", &UiImage::tint, {.min = 0.0, .max = 1.0, .hint = FieldHint::Color})
        .field("border", &UiImage::border,
               {.description = "Nine-slice borders in image pixels; all zero stretches the image."})
        .field("borderScale", &UiImage::borderScale,
               {.description = "Canvas units per image pixel of the borders.", .min = 0.0, .softMax = 8.0})
        .field("preserveAspect", &UiImage::preserveAspect,
               {.description = "Fit the image inside the element keeping its aspect."});
    reflect<UiText>("illusion.UiText", 1, registry)
        .meta({.displayName = "UI text",
               .category = "UI",
               .icon = "text_fields",
               .description = "Draws text, literal or from the game's string tables.",
               .docsAnchor = kDocsText,
               .requiredComponents = kRequiresElement})
        .field("text", &UiText::text,
               {.description = "Shown when key is empty.", .hint = FieldHint::Multiline})
        .field("key", &UiText::key, {.description = "String table key whose text is shown instead."})
        .field("font", &UiText::font, {.assetKind = AssetKind::Font})
        .field("size", &UiText::size,
               {.description = "Em size in canvas units.", .min = 0.0, .softMin = 8.0, .softMax = 128.0})
        .field("color", &UiText::color, {.min = 0.0, .max = 1.0, .hint = FieldHint::Color})
        .field("align", &UiText::align)
        .field("verticalAlign", &UiText::verticalAlign)
        .field("wrap", &UiText::wrap, {.description = "Wrap lines at the element's width."})
        .field("lineSpacing", &UiText::lineSpacing,
               {.description = "Multiplier of the font's line height.", .min = 0.0, .softMax = 3.0})
        .field("letterSpacing", &UiText::letterSpacing,
               {.description = "Extra advance per glyph in ems.", .softMin = -0.2, .softMax = 1.0});
    reflect<UiStack>("illusion.UiStack", 1, registry)
        .meta({.displayName = "UI stack",
               .category = "UI",
               .icon = "view_agenda",
               .description = "Places the element's children one after another.",
               .docsAnchor = kDocsLayout,
               .requiredComponents = kRequiresElement})
        .field("axis", &UiStack::axis)
        .field("spacing", &UiStack::spacing,
               {.description = "Gap between children in canvas units.", .min = 0.0, .softMax = 64.0})
        .field("padding", &UiStack::padding)
        .field("crossAlign", &UiStack::crossAlign);
    reflect<UiGrid>("illusion.UiGrid", 1, registry)
        .meta({.displayName = "UI grid",
               .category = "UI",
               .icon = "grid_view",
               .description = "Places the element's children in rows of equal cells.",
               .docsAnchor = kDocsLayout,
               .requiredComponents = kRequiresElement})
        .field("columns", &UiGrid::columns, {.min = 1.0, .softMax = 16.0})
        .field("cellSize", &UiGrid::cellSize,
               {.description = "Size of each cell in canvas units.", .min = 0.0})
        .field("spacing", &UiGrid::spacing,
               {.description = "Gaps between columns and rows.", .min = 0.0, .softMax = 64.0})
        .field("padding", &UiGrid::padding);
    reflectRuntime<UiComputedRect>("illusion.UiComputedRect", {.displayName = "UI computed rectangle"},
                                   registry);

    reflect<UiInteractable>("illusion.UiInteractable", 1, registry)
        .meta({.displayName = "UI interactable",
               .category = "UI",
               .icon = "touch_app",
               .description = "Makes an element react to the pointer, the keyboard and gamepads.",
               .docsAnchor = kDocsInteraction,
               .requiredComponents = kRequiresElement})
        .field("enabled", &UiInteractable::enabled,
               {.description = "A disabled element ignores input and cannot be focused."})
        .field("focusable", &UiInteractable::focusable,
               {.description = "Reachable by navigation with the keyboard and gamepads."})
        .field("hoveredTint", &UiInteractable::hoveredTint,
               {.description = "Multiplies the image's tint under the pointer.",
                .min = 0.0,
                .softMax = 2.0,
                .hint = FieldHint::Color})
        .field("pressedTint", &UiInteractable::pressedTint,
               {.description = "Multiplies the image's tint while pressed.",
                .min = 0.0,
                .softMax = 2.0,
                .hint = FieldHint::Color})
        .field("focusedTint", &UiInteractable::focusedTint,
               {.description = "Multiplies the image's tint while focused.",
                .min = 0.0,
                .softMax = 2.0,
                .hint = FieldHint::Color})
        .field("disabledTint", &UiInteractable::disabledTint,
               {.description = "Multiplies the image's tint while disabled.",
                .min = 0.0,
                .softMax = 2.0,
                .hint = FieldHint::Color})
        .field("up", &UiInteractable::up,
               {.description = "Focused by navigating up; none picks the nearest element above."})
        .field("down", &UiInteractable::down,
               {.description = "Focused by navigating down; none picks the nearest element below."})
        .field("left", &UiInteractable::left,
               {.description = "Focused by navigating left; none picks the nearest element there."})
        .field("right", &UiInteractable::right,
               {.description = "Focused by navigating right; none picks the nearest element there."});
    reflect<UiButton>("illusion.UiButton", 1, registry)
        .meta({.displayName = "UI button",
               .category = "UI",
               .icon = "smart_button",
               .description = "Reports a click when the element is activated.",
               .docsAnchor = kDocsInteraction,
               .requiredComponents = kRequiresInteractable});
    reflect<UiToggle>("illusion.UiToggle", 1, registry)
        .meta({.displayName = "UI toggle",
               .category = "UI",
               .icon = "toggle_on",
               .description = "A value that activation switches; the first child shows while it is on.",
               .docsAnchor = kDocsInteraction,
               .requiredComponents = kRequiresInteractable})
        .field("on", &UiToggle::on, {.description = "The value."});
    reflect<UiSlider>("illusion.UiSlider", 1, registry)
        .meta({.displayName = "UI slider",
               .category = "UI",
               .icon = "linear_scale",
               .description = "A value in a range; the first child is the fill, the second the handle.",
               .docsAnchor = kDocsInteraction,
               .requiredComponents = kRequiresInteractable})
        .field("value", &UiSlider::value, {.description = "The value, kept between min and max."})
        .field("min", &UiSlider::min, {.description = "Value at the left or top."})
        .field("max", &UiSlider::max, {.description = "Value at the right or bottom."})
        .field("step", &UiSlider::step,
               {.description = "Values snap to multiples of it from min; 0 is continuous.", .min = 0.0})
        .field("axis", &UiSlider::axis)
        .field("navigationStep", &UiSlider::navigationStep,
               {.description = "Change per navigation press as a fraction of the range, when step is 0.",
                .min = 0.0,
                .max = 1.0,
                .hint = FieldHint::Slider});
    reflectRuntime<UiInteraction>("illusion.UiInteraction", {.displayName = "UI interaction"}, registry);
    reflectRuntime<UiTextArguments>("illusion.UiTextArguments", {.displayName = "UI text arguments"},
                                    registry);
}

} // namespace illusion
