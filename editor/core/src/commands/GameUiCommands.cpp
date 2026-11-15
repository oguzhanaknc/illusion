// The gameui.* commands (ADR 0057): create game UI elements with defaults, read the computed layout of the
// edit or the play World, and click and focus elements of the running game. The editor's own UI is ui.*.

#include "CommandSupport.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/editor/core/UiPlacementEdit.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>

namespace illusion::editor::detail
{
namespace
{

constexpr u32 kDefaultWidth = 1920;
constexpr u32 kDefaultHeight = 1080;
constexpr u32 kMaxTargetSide = 16384;
// Where created elements go in their parent, in canvas units.
constexpr Vec2 kElementOrigin{24.0f, 24.0f};

constexpr std::array<std::string_view, 10> kKindNames{
    "Canvas", "Panel", "Text", "Button", "Toggle", "Slider", "Text Field", "Scroll View", "Stack", "Grid"};

template <class E>
Expected<void, CommandError> checkEnum(E value, E last, std::string pointer)
{
    if (static_cast<usize>(value) > static_cast<usize>(last))
    {
        return unexpected(CommandError::invalid(std::move(pointer), "is not one of the allowed values"));
    }
    return {};
}

bool isUiEntity(const World& world, Entity entity)
{
    return world.has<UiCanvas>(entity) || world.has<UiElement>(entity);
}

// The first canvas in hierarchy order (roots in Outliner order, depth first), or null.
Entity firstCanvas(CommandContext& context, Entity parent = {})
{
    std::vector<Entity> children;
    context.index().children(parent, children);
    for (const Entity child : children)
    {
        if (context.world().has<UiCanvas>(child))
        {
            return child;
        }
        if (const Entity found = firstCanvas(context, child); !found.isNull())
        {
            return found;
        }
    }
    return {};
}

// The first font of the project's assets, for new texts ("" when there is none).
std::string defaultFont(CommandContext& context)
{
    if (context.editor().project() == nullptr)
    {
        return {};
    }
    for (const auto& [path, record] : context.editor().assets().records())
    {
        if (record.kind == "Font")
        {
            return path;
        }
    }
    return {};
}

// The Components of one Entity to create, as the JSON object WorldEditor::create() reads.
class ComponentSet
{
public:
    explicit ComponentSet(const ComponentRegistry& registry)
        : m_registry(&registry)
        , m_root(m_doc.makeObject())
    {
    }

    template <class T>
    void add(const T& value)
    {
        const TypeReflection* reflection = m_registry->reflection(componentId<T>());
        if (reflection == nullptr)
        {
            m_failed = true;
            return;
        }
        Expected<JsonMutValue, ValueError> written = writeValue(m_doc, *reflection, &value);
        if (!written)
        {
            m_failed = true;
            return;
        }
        m_root.add(reflection->name, *written);
    }

    [[nodiscard]] Expected<JsonDocument, CommandError> finish()
    {
        std::string text;
        if (m_failed || !m_root.write(text))
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "the UI Components are not reflected"));
        }
        Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(text);
        if (!parsed)
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "cannot read the UI Components"));
        }
        return std::move(parsed).value();
    }

private:
    const ComponentRegistry* m_registry;
    JsonMutDocument m_doc;
    JsonMutValue m_root;
    bool m_failed = false;
};

UiElement placed(Vec2 size)
{
    UiElement element;
    element.placement.offsetMin = kElementOrigin;
    element.placement.offsetMax = kElementOrigin + size;
    element.size = size;
    return element;
}

// Fills its parent, inset by @p inset on every side.
UiElement stretched(f32 inset = 0.0f)
{
    UiElement element;
    element.placement.anchorMax = Vec2(1.0f);
    element.placement.offsetMin = Vec2(inset);
    element.placement.offsetMax = Vec2(-inset);
    return element;
}

UiImage tinted(Vec4 tint)
{
    UiImage image;
    image.tint = tint;
    return image;
}

UiText textOf(std::string value, const std::string& font, f32 size)
{
    UiText text;
    text.text = std::move(value);
    text.font = font.empty() ? AssetRef{} : AssetRef(font);
    text.size = size;
    return text;
}

constexpr Vec4 kPanelTint{0.08f, 0.09f, 0.12f, 0.75f};
constexpr Vec4 kButtonTint{0.18f, 0.38f, 0.78f, 1.0f};
constexpr Vec4 kTrackTint{0.14f, 0.15f, 0.19f, 1.0f};
constexpr Vec4 kAccentTint{0.3f, 0.75f, 0.45f, 1.0f};

// One Entity of the element: its role ("" for the root), name and Components.
struct Piece
{
    std::string_view role;
    std::string name;
    JsonDocument components;
};

Expected<void, CommandError> addPiece(std::vector<Piece>& pieces, std::string_view role, std::string name,
                                      ComponentSet& set)
{
    Expected<JsonDocument, CommandError> components = set.finish();
    if (!components)
    {
        return unexpected(std::move(components).error());
    }
    pieces.push_back({role, std::move(name), std::move(components).value()});
    return {};
}

// The Entities of an element of @p kind: the root first, then its children.
Expected<std::vector<Piece>, CommandError> piecesOf(GameUiKind kind, const std::string& name,
                                                    const std::optional<std::string>& label,
                                                    const std::string& font,
                                                    const ComponentRegistry& registry)
{
    std::vector<Piece> pieces;
    ComponentSet root(registry);
    const auto labelOr = [&label](std::string_view fallback) {
        return label ? *label : std::string(fallback);
    };
    Expected<void, CommandError> added;
    switch (kind)
    {
    case GameUiKind::Canvas:
        root.add(UiCanvas{});
        break;
    case GameUiKind::Panel:
        root.add(placed({400.0f, 300.0f}));
        root.add(tinted(kPanelTint));
        break;
    case GameUiKind::Text:
    {
        UiElement element = placed({320.0f, 48.0f});
        element.size = Vec2(0.0f); // sized by its content inside a stack or grid
        root.add(element);
        root.add(textOf(labelOr("Text"), font, 32.0f));
        break;
    }
    case GameUiKind::Button:
    {
        root.add(placed({240.0f, 64.0f}));
        root.add(tinted(kButtonTint));
        root.add(UiInteractable{});
        root.add(UiButton{});
        added = addPiece(pieces, "", name, root);
        ComponentSet child(registry);
        child.add(stretched());
        UiText caption = textOf(labelOr("Button"), font, 28.0f);
        caption.align = UiTextAlign::Center;
        caption.verticalAlign = UiVerticalAlign::Middle;
        caption.wrap = false;
        child.add(caption);
        if (added)
        {
            added = addPiece(pieces, "label", "Label", child);
        }
        return added ? Expected<std::vector<Piece>, CommandError>(std::move(pieces))
                     : unexpected(std::move(added).error());
    }
    case GameUiKind::Toggle:
    {
        root.add(placed({48.0f, 48.0f}));
        root.add(tinted(kTrackTint));
        root.add(UiInteractable{});
        root.add(UiToggle{});
        added = addPiece(pieces, "", name, root);
        ComponentSet check(registry);
        check.add(stretched(10.0f));
        check.add(tinted(kAccentTint));
        if (added)
        {
            added = addPiece(pieces, "check", "Check", check);
        }
        return added ? Expected<std::vector<Piece>, CommandError>(std::move(pieces))
                     : unexpected(std::move(added).error());
    }
    case GameUiKind::Slider:
    {
        root.add(placed({320.0f, 24.0f}));
        root.add(tinted(kTrackTint));
        root.add(UiInteractable{});
        root.add(UiSlider{});
        added = addPiece(pieces, "", name, root);
        ComponentSet fill(registry);
        fill.add(stretched());
        fill.add(tinted(kButtonTint));
        // The handle keeps its size and cross-axis placement; the slider centres it on the value.
        UiElement knob;
        knob.placement.anchorMax = {0.0f, 1.0f};
        knob.placement.offsetMin = {-8.0f, -6.0f};
        knob.placement.offsetMax = {8.0f, 6.0f};
        ComponentSet handle(registry);
        handle.add(knob);
        handle.add(tinted(Vec4(1.0f)));
        if (added)
        {
            added = addPiece(pieces, "fill", "Fill", fill);
        }
        if (added)
        {
            added = addPiece(pieces, "handle", "Handle", handle);
        }
        return added ? Expected<std::vector<Piece>, CommandError>(std::move(pieces))
                     : unexpected(std::move(added).error());
    }
    case GameUiKind::TextField:
    {
        root.add(placed({320.0f, 48.0f}));
        root.add(tinted(kTrackTint));
        root.add(UiInteractable{});
        UiText shown = textOf({}, font, 24.0f);
        shown.verticalAlign = UiVerticalAlign::Middle;
        shown.wrap = false;
        root.add(shown);
        UiTextField field;
        field.placeholder = labelOr("Enter text");
        root.add(field);
        break;
    }
    case GameUiKind::ScrollView:
    {
        root.add(placed({400.0f, 300.0f}));
        root.add(tinted(kPanelTint));
        root.add(UiScrollView{});
        added = addPiece(pieces, "", name, root);
        UiStack stack;
        stack.spacing = 8.0f;
        stack.padding = {8.0f, 8.0f, 8.0f, 8.0f};
        ComponentSet content(registry);
        content.add(UiElement{});
        content.add(stack);
        if (added)
        {
            added = addPiece(pieces, "content", "Content", content);
        }
        return added ? Expected<std::vector<Piece>, CommandError>(std::move(pieces))
                     : unexpected(std::move(added).error());
    }
    case GameUiKind::Stack:
    {
        root.add(placed({320.0f, 400.0f}));
        UiStack stack;
        stack.spacing = 8.0f;
        root.add(stack);
        break;
    }
    case GameUiKind::Grid:
        root.add(placed({440.0f, 440.0f}));
        root.add(UiGrid{});
        break;
    }
    added = addPiece(pieces, "", name, root);
    return added ? Expected<std::vector<Piece>, CommandError>(std::move(pieces))
                 : unexpected(std::move(added).error());
}

// The parent of a new element: the given one (a canvas or UI element), else the selected canvas or UI
// element, else the first canvas. A null result means that a canvas has to be created.
Expected<Entity, CommandError> elementParent(CommandContext& context, const GameUiCreateParams& params)
{
    World& world = context.world();
    if (params.parent)
    {
        Expected<Entity, CommandError> parent = context.entity(*params.parent, "/parent");
        if (!parent)
        {
            return parent;
        }
        if (!isUiEntity(world, *parent))
        {
            return unexpected(CommandError::invalid(
                "/parent", "must be a canvas or a UI element (an entity with illusion.UiCanvas or "
                           "illusion.UiElement)"));
        }
        return parent;
    }
    if (const EntityGuid selected = context.editor().selection().primary(); !selected.isNull())
    {
        if (const Entity entity = context.index().find(selected);
            !entity.isNull() && world.isAlive(entity) && isUiEntity(world, entity))
        {
            return entity;
        }
    }
    return firstCanvas(context);
}

Expected<GameUiCreateResult, CommandError> gameUiCreate(CommandContext& context,
                                                        const GameUiCreateParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(params.kind, GameUiKind::Grid, "/kind"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (params.name && !params.name->empty())
    {
        if (const std::string_view problem = entityNameError(*params.name); !problem.empty())
        {
            return unexpected(CommandError::invalid("/name", std::string(problem)));
        }
    }
    const std::string name = params.name.value_or(std::string(kKindNames[static_cast<usize>(params.kind)]));
    const ComponentRegistry& registry = context.world().registry();
    GameUiCreateResult result;

    Entity parent;
    if (params.kind == GameUiKind::Canvas)
    {
        if (params.parent)
        {
            Expected<Entity, CommandError> found = context.entity(*params.parent, "/parent");
            if (!found)
            {
                return unexpected(std::move(found).error());
            }
            parent = *found;
        }
    }
    else
    {
        Expected<Entity, CommandError> found = elementParent(context, params);
        if (!found)
        {
            return unexpected(std::move(found).error());
        }
        parent = *found;
        if (parent.isNull())
        {
            Expected<std::vector<Piece>, CommandError> canvas =
                piecesOf(GameUiKind::Canvas, "Canvas", std::nullopt, {}, registry);
            if (!canvas || canvas->empty())
            {
                return unexpected(
                    CommandError::make(ErrorKind::Internal, "the UI Components are not reflected"));
            }
            CreateEntityOptions options;
            options.name = canvas->front().name;
            options.components = canvas->front().components.root();
            Expected<Entity, CommandError> created = context.edit().create(options);
            if (!created)
            {
                return unexpected(std::move(created).error());
            }
            parent = *created;
            result.canvas = context.summary(parent);
        }
    }

    Expected<std::vector<Piece>, CommandError> pieces =
        piecesOf(params.kind, name, params.text, defaultFont(context), registry);
    if (!pieces || pieces->empty())
    {
        return unexpected(pieces
                              ? CommandError::make(ErrorKind::Internal, "the UI Components are not reflected")
                              : std::move(pieces).error());
    }
    Entity root;
    for (const Piece& piece : *pieces)
    {
        CreateEntityOptions options;
        options.name = piece.name;
        options.parent = root.isNull() ? parent : root;
        options.components = piece.components.root();
        Expected<Entity, CommandError> created = context.edit().create(options);
        if (!created)
        {
            return unexpected(std::move(created).error());
        }
        if (root.isNull())
        {
            root = *created;
            continue;
        }
        result.parts.push_back({std::string(piece.role), context.summary(*created)});
    }
    result.entity = context.summary(root);
    return result;
}

// Fonts and image sizes for laying out the edit World without a renderer: fonts are baked (or read from
// the import cache) on first use, images are read once for their pixel size; nothing is drawn.
class CpuUiAssets final : public UiAssets
{
public:
    CpuUiAssets(VirtualFileSystem& files, bool cache)
        : m_database(files, cache ? "cache" : "")
    {
    }

    UiFontFace font(const AssetRef& font) override
    {
        if (!font)
        {
            return {};
        }
        const std::string path(font.path());
        auto found = m_fonts.find(path);
        if (found == m_fonts.end())
        {
            found = m_fonts.emplace(path, m_database.loadFont(path)).first;
        }
        return found->second ? UiFontFace{.font = &*found->second, .atlas = {}} : UiFontFace{};
    }

    UiImageSource image(const AssetRef& image) override
    {
        if (!image)
        {
            return {};
        }
        const std::string path(image.path());
        auto found = m_images.find(path);
        if (found == m_images.end())
        {
            const std::optional<Image> loaded = m_database.loadImage(path);
            found = m_images
                        .emplace(path, loaded ? Vec2(static_cast<f32>(loaded->width()),
                                                     static_cast<f32>(loaded->height()))
                                              : Vec2(0.0f))
                        .first;
        }
        return {.texture = {}, .size = found->second};
    }

private:
    AssetDatabase m_database;
    std::map<std::string, std::optional<Font>, std::less<>> m_fonts;
    std::map<std::string, Vec2, std::less<>> m_images;
};

UiRect boundsOf(const UiTransform2D& transform, const UiRect& rect) noexcept
{
    const std::array corners{transform.apply(rect.min), transform.apply(Vec2(rect.max.x, rect.min.y)),
                             transform.apply(rect.max), transform.apply(Vec2(rect.min.x, rect.max.y))};
    UiRect bounds{corners[0], corners[0]};
    for (const Vec2 corner : corners)
    {
        bounds.min = glm::min(bounds.min, corner);
        bounds.max = glm::max(bounds.max, corner);
    }
    return bounds;
}

// The element of @p layout for @p entity, or nullptr.
const UiLayoutElement* elementOf(const UiLayout& layout, Entity entity)
{
    const auto found =
        std::find_if(layout.elements.begin(), layout.elements.end(),
                     [entity](const UiLayoutElement& element) { return element.entity == entity; });
    return found != layout.elements.end() ? &*found : nullptr;
}

// True when @p element is @p ancestor or lies below it.
bool within(const UiLayout& layout, const UiLayoutElement& element, u32 ancestor)
{
    for (u32 at = static_cast<u32>(&element - layout.elements.data()); at != UiLayoutElement::kNoParent;
         at = layout.elements[at].parent)
    {
        if (at == ancestor)
        {
            return true;
        }
    }
    return false;
}

Expected<void, CommandError> checkTarget(const std::optional<u32>& width, const std::optional<u32>& height)
{
    if (width && (*width == 0 || *width > kMaxTargetSide))
    {
        return unexpected(CommandError::invalid("/width", "must be between 1 and 16384"));
    }
    if (height && (*height == 0 || *height > kMaxTargetSide))
    {
        return unexpected(CommandError::invalid("/height", "must be between 1 and 16384"));
    }
    return {};
}

// The layout of the World the command works on: the edit World laid out for width x height pixels, or the
// play World as its last update laid it out.
struct LaidOut
{
    UiLayout layout;
    UVec2 target{0};
    UiWorld* play = nullptr; // the running game's UI (null for the edit World)
};

Expected<LaidOut, CommandError> layOut(CommandContext& context, const std::optional<u32>& width,
                                       const std::optional<u32>& height)
{
    LaidOut out;
    if (context.onPlayWorld())
    {
        PlayHost* play = context.editor().play();
        out.play = play != nullptr ? play->gameUi() : nullptr;
        if (out.play == nullptr)
        {
            return unexpected(CommandError::make(ErrorKind::Unsupported, "the running game has no UI"));
        }
        out.target = play->gameUiFrame().targetPixels;
        if (width || height)
        {
            context.warn(std::format("width and height apply to the edit World; the play World is laid out "
                                     "for its UI target of {}x{} pixels",
                                     out.target.x, out.target.y));
        }
        out.play->layout(out.layout);
        return out;
    }
    out.target = {width.value_or(kDefaultWidth), height.value_or(kDefaultHeight)};
    UiWorldDesc desc;
    desc.preview = true;
    UiWorld preview(context.world(), desc);
    CpuUiAssets assets(context.editor().files(), context.editor().project() != nullptr);
    preview.setAssets(&assets);
    if (PlayHost* play = context.editor().play())
    {
        preview.setLocalization(play->projectLocalization());
    }
    preview.update(UiFrame{.targetPixels = out.target, .safeInsetPixels = {}});
    preview.layout(out.layout);
    return out;
}

Expected<GameUiLayoutResult, CommandError> gameUiLayout(CommandContext& context,
                                                        const GameUiLayoutParams& params)
{
    if (Expected<void, CommandError> valid = checkTarget(params.width, params.height); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    World& world = context.world();
    Entity scope;
    if (params.entity)
    {
        Expected<Entity, CommandError> entity = context.entity(*params.entity, "/entity");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        if (!isUiEntity(world, *entity))
        {
            return unexpected(CommandError::make(
                ErrorKind::PreconditionFailed, "the entity has no illusion.UiCanvas or illusion.UiElement"));
        }
        scope = *entity;
    }

    Expected<LaidOut, CommandError> laidOut = layOut(context, params.width, params.height);
    if (!laidOut)
    {
        return unexpected(std::move(laidOut).error());
    }
    const UiLayout& layout = laidOut->layout;
    GameUiLayoutResult result;
    result.world = laidOut->play != nullptr ? "play" : "edit";
    result.width = laidOut->target.x;
    result.height = laidOut->target.y;

    // The scope as an element index (or canvas index for a canvas).
    std::optional<u32> scopeElement;
    std::optional<u32> scopeCanvas;
    if (!scope.isNull())
    {
        if (const UiLayoutElement* element = elementOf(layout, scope))
        {
            scopeElement = static_cast<u32>(element - layout.elements.data());
        }
        for (u32 c = 0; c < layout.canvases.size(); ++c)
        {
            if (layout.canvases[c].entity == scope)
            {
                scopeCanvas = c;
            }
        }
        if (!scopeElement && !scopeCanvas)
        {
            context.warn("the entity is not laid out: it or an ancestor is inactive, or it is not under an "
                         "active canvas");
        }
    }
    const Entity focus = laidOut->play != nullptr ? laidOut->play->focused() : Entity{};
    for (u32 c = 0; c < layout.canvases.size(); ++c)
    {
        const UiLayoutCanvas& canvas = layout.canvases[c];
        if (!scope.isNull() && scopeCanvas != c &&
            !(scopeElement && layout.elements[*scopeElement].canvas == c))
        {
            continue;
        }
        result.canvases.push_back({.entity = context.summary(canvas.entity),
                                   .order = canvas.order,
                                   .modal = canvas.modal,
                                   .pixelsPerUnit = canvas.metrics.pixelsPerUnit,
                                   .size = canvas.metrics.size,
                                   .areaMin = canvas.area.min,
                                   .areaMax = canvas.area.max});
    }
    for (const UiLayoutElement& element : layout.elements)
    {
        if (!scope.isNull() && !(scopeCanvas && element.canvas == *scopeCanvas) &&
            !(scopeElement && within(layout, element, *scopeElement)))
        {
            continue;
        }
        const UiLayoutCanvas& canvas = layout.canvases[element.canvas];
        const UiRect bounds = boundsOf(element.transform, element.rect);
        GameUiElementInfo info{.entity = context.summary(element.entity),
                               .canvas = context.summary(canvas.entity).id,
                               .min = element.rect.min,
                               .max = element.rect.max,
                               .pixelMin = bounds.min * canvas.metrics.pixelsPerUnit,
                               .pixelMax = bounds.max * canvas.metrics.pixelsPerUnit,
                               .opacity = element.opacity,
                               .visible = element.visible,
                               .interactable = std::nullopt,
                               .focused = std::nullopt};
        if (const UiInteractable* interactable = world.tryGet<UiInteractable>(element.entity))
        {
            info.interactable = interactable->enabled;
        }
        if (context.onPlayWorld())
        {
            info.focused = element.entity == focus;
        }
        result.elements.push_back(std::move(info));
    }
    return result;
}

// The play World's UI and the entity a gameui.click or gameui.focus names in it.
struct PlayTarget
{
    PlayHost* play = nullptr;
    UiWorld* ui = nullptr;
    EntityIndex* index = nullptr;
};

Expected<PlayTarget, CommandError> playTarget(CommandContext& context)
{
    EditorContext& editor = context.editor();
    PlayTarget target{.play = editor.play(), .ui = nullptr, .index = editor.playEntities()};
    if (target.play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    target.ui = target.play->gameUi();
    if (target.ui == nullptr || target.index == nullptr || editor.playWorld() == nullptr)
    {
        return unexpected(
            CommandError::make(ErrorKind::WrongMode, "no game runs; start one with play.start"));
    }
    return target;
}

std::string_view eventName(UiEventType type) noexcept
{
    switch (type)
    {
    case UiEventType::Clicked:
        return "clicked";
    case UiEventType::ValueChanged:
        return "value_changed";
    case UiEventType::FocusChanged:
        return "focus_changed";
    case UiEventType::Cancelled:
        return "cancelled";
    case UiEventType::TextChanged:
        return "text_changed";
    case UiEventType::Submitted:
        return "submitted";
    }
    return "clicked";
}

CommandError blocked(std::string message)
{
    return CommandError::make(ErrorKind::PreconditionFailed, std::move(message));
}

// Why the pointer cannot click @p entity at @p pixels, or nothing when it can.
std::optional<CommandError> clickBlocker(World& world, EntityIndex& index, UiWorld& ui,
                                         const UiLayout& layout, const UiLayoutElement& element, Vec2 pixels,
                                         UVec2 target)
{
    if (!element.visible)
    {
        return blocked(element.opacity <= 0.0f ? "the element is hidden: its opacity is 0"
                                               : "the element is hidden by a toggle that is off");
    }
    const UiInteractable* interactable = world.tryGet<UiInteractable>(element.entity);
    if (interactable == nullptr)
    {
        return blocked("the element has no illusion.UiInteractable, so the pointer does not react to it");
    }
    if (!interactable->enabled)
    {
        return blocked("the element is disabled (illusion.UiInteractable enabled is false)");
    }
    if (pixels.x < 0.0f || pixels.y < 0.0f || pixels.x >= static_cast<f32>(target.x) ||
        pixels.y >= static_cast<f32>(target.y))
    {
        return blocked(
            std::format("the centre of the element ({}, {}) is outside the UI target of {}x{} pixels",
                        pixels.x, pixels.y, target.x, target.y));
    }
    if (!layout.canvases[element.canvas].pointerEligible)
    {
        for (const UiLayoutCanvas& canvas : layout.canvases)
        {
            if (canvas.modal && canvas.pointerEligible)
            {
                return blocked("the element is below the modal canvas " + index.summary(canvas.entity).path);
            }
        }
        return blocked("the element is below a modal canvas");
    }
    const Entity hit = ui.hitTest(pixels);
    if (hit == element.entity)
    {
        return std::nullopt;
    }
    if (!hit.isNull())
    {
        const EntitySummary cover = index.summary(hit);
        return blocked(std::format("the element is covered at its centre by {} ({})", cover.path, cover.id));
    }
    const f32 pixelsPerUnit = layout.canvases[element.canvas].metrics.pixelsPerUnit;
    for (u32 at = element.parent; at != UiLayoutElement::kNoParent; at = layout.elements[at].parent)
    {
        const UiLayoutElement& ancestor = layout.elements[at];
        const UiRect clip = boundsOf(ancestor.transform, ancestor.rect);
        if (ancestor.clipsChildren &&
            !UiRect{clip.min * pixelsPerUnit, clip.max * pixelsPerUnit}.contains(pixels))
        {
            return blocked("the centre of the element is outside the clip of " +
                           index.summary(ancestor.entity).path);
        }
    }
    return blocked("the pointer does not hit the element at its centre");
}

Expected<GameUiClickResult, CommandError> gameUiClick(CommandContext& context,
                                                      const GameUiClickParams& params)
{
    Expected<PlayTarget, CommandError> target = playTarget(context);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    EditorContext& editor = context.editor();
    Expected<Entity, CommandError> entity =
        target->index->resolve(params.entity, editor.selection(), "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    World& world = *editor.playWorld();
    if (world.has<UiCanvas>(*entity) || !world.has<UiElement>(*entity))
    {
        return unexpected(blocked("the entity is not a UI element (it needs illusion.UiElement)"));
    }
    UiLayout layout;
    target->ui->layout(layout);
    const UiLayoutElement* element = elementOf(layout, *entity);
    if (element == nullptr)
    {
        return unexpected(blocked("the element is not laid out: it or an ancestor is inactive, or it is not "
                                  "under an active canvas"));
    }
    const UiFrame frame = target->play->gameUiFrame();
    const f32 pixelsPerUnit = layout.canvases[element->canvas].metrics.pixelsPerUnit;
    const Vec2 pixels =
        element->transform.apply((element->rect.min + element->rect.max) * 0.5f) * pixelsPerUnit;
    if (std::optional<CommandError> blocker =
            clickBlocker(world, *target->index, *target->ui, layout, *element, pixels, frame.targetPixels))
    {
        return unexpected(std::move(*blocker));
    }
    GameUiClickResult result;
    result.entity = target->index->summary(*entity);
    result.pixels = pixels;
    if (context.dryRun())
    {
        return result;
    }
    std::vector<UiEvent> events;
    result.updates = target->play->clickUi(pixels, events);
    // The session may have ended (the game asked to exit after the click).
    EntityIndex* index = editor.playEntities();
    for (const UiEvent& event : events)
    {
        GameUiEventInfo info{
            .type = std::string(eventName(event.type)), .entity = std::nullopt, .value = event.value};
        if (index != nullptr && !event.entity.isNull() && index->world().isAlive(event.entity))
        {
            info.entity = index->summary(event.entity);
        }
        result.events.push_back(std::move(info));
    }
    return result;
}

Expected<GameUiFocusResult, CommandError> gameUiFocus(CommandContext& context,
                                                      const GameUiFocusParams& params)
{
    Expected<PlayTarget, CommandError> target = playTarget(context);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    EditorContext& editor = context.editor();
    Entity entity;
    if (params.entity)
    {
        Expected<Entity, CommandError> resolved =
            target->index->resolve(*params.entity, editor.selection(), "/entity");
        if (!resolved)
        {
            return unexpected(std::move(resolved).error());
        }
        entity = *resolved;
        if (!editor.playWorld()->has<UiElement>(entity) || editor.playWorld()->has<UiCanvas>(entity))
        {
            return unexpected(blocked("the entity is not a UI element (it needs illusion.UiElement)"));
        }
    }
    if (!context.dryRun())
    {
        target->ui->setFocus(entity);
        if (!entity.isNull() && target->ui->focused() != entity)
        {
            context.warn("the element cannot take the focus: it must be laid out on a canvas the focus can "
                         "reach, visible, and have an enabled, focusable illusion.UiInteractable");
        }
    }
    GameUiFocusResult result;
    if (const Entity focused = target->ui->focused();
        !focused.isNull() && editor.playWorld()->isAlive(focused))
    {
        result.focused = target->index->summary(focused);
    }
    return result;
}

// The elements of canvas @p canvas of @p layout alone, with their parents renumbered.
UiLayout canvasLayout(const UiLayout& layout, u32 canvas)
{
    UiLayout out;
    out.canvases.push_back(layout.canvases[canvas]);
    u32 first = UiLayoutElement::kNoParent;
    for (u32 i = 0; i < layout.elements.size(); ++i)
    {
        UiLayoutElement element = layout.elements[i];
        if (element.canvas != canvas)
        {
            continue;
        }
        first = std::min(first, i);
        element.canvas = 0;
        if (element.parent != UiLayoutElement::kNoParent)
        {
            element.parent -= first;
        }
        out.elements.push_back(element);
    }
    return out;
}

Expected<GameUiPickResult, CommandError> gameUiPick(CommandContext& context, const GameUiPickParams& params)
{
    if (!std::isfinite(params.x))
    {
        return unexpected(CommandError::invalid("/x", "must be a finite number"));
    }
    if (!std::isfinite(params.y))
    {
        return unexpected(CommandError::invalid("/y", "must be a finite number"));
    }
    if (Expected<void, CommandError> valid = checkTarget(params.width, params.height); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<LaidOut, CommandError> laidOut = layOut(context, params.width, params.height);
    if (!laidOut)
    {
        return unexpected(std::move(laidOut).error());
    }
    const UiLayout& layout = laidOut->layout;
    GameUiPickResult result;
    const Vec2 pixels(params.x, params.y);
    for (u32 c = static_cast<u32>(layout.canvases.size()); c-- > 0;)
    {
        const f32 pixelsPerUnit = layout.canvases[c].metrics.pixelsPerUnit;
        const Entity hit = pickUiElement(canvasLayout(layout, c), pixels / pixelsPerUnit);
        if (hit.isNull())
        {
            continue;
        }
        result.entity = context.summary(hit);
        for (const UiLayoutElement* at = elementOf(layout, hit); at != nullptr;
             at = at->parent != UiLayoutElement::kNoParent ? &layout.elements[at->parent] : nullptr)
        {
            result.path.push_back(context.summary(at->entity));
        }
        result.path.push_back(context.summary(layout.canvases[c].entity));
        std::reverse(result.path.begin(), result.path.end());
        break;
    }
    return result;
}

// A UI element of the World the command works on, as the layout placed it.
struct PlacedElement
{
    Entity entity{};
    const UiLayoutElement* laidOut = nullptr; // null when the layout skipped it
    UiRect parent{};                          // the rectangle it is placed in
    bool byLayout = false;                    // a child of a UiStack or UiGrid
};

Expected<Entity, CommandError> uiElement(CommandContext& context, const std::string& reference)
{
    Expected<Entity, CommandError> entity = context.entity(reference, "/entity");
    if (!entity)
    {
        return entity;
    }
    if (context.world().has<UiCanvas>(*entity) || !context.world().has<UiElement>(*entity))
    {
        return unexpected(blocked("the entity is not a UI element (it needs illusion.UiElement)"));
    }
    return entity;
}

PlacedElement placedElement(const World& world, const UiLayout& layout, Entity entity)
{
    PlacedElement out;
    out.entity = entity;
    const Entity parent = parentOf(world, entity);
    out.byLayout = !parent.isNull() && (world.has<UiStack>(parent) || world.has<UiGrid>(parent));
    out.laidOut = elementOf(layout, entity);
    if (out.laidOut != nullptr)
    {
        out.parent = out.laidOut->parent != UiLayoutElement::kNoParent
                         ? layout.elements[out.laidOut->parent].rect
                         : layout.canvases[out.laidOut->canvas].area;
    }
    return out;
}

CommandError notLaidOut()
{
    return blocked("the element is not laid out: it or an ancestor is inactive, or it is not under an active "
                   "canvas");
}

Expected<GameUiPlacementResult, CommandError> writeElement(CommandContext& context, Entity entity,
                                                           const UiElement& element, bool byLayout)
{
    Expected<bool, CommandError> written =
        context.edit().assignComponent(entity, componentId<UiElement>(), &element);
    if (!written)
    {
        return unexpected(std::move(written).error());
    }
    return GameUiPlacementResult{.entity = context.summary(entity),
                                 .anchorMin = element.placement.anchorMin,
                                 .anchorMax = element.placement.anchorMax,
                                 .offsetMin = element.placement.offsetMin,
                                 .offsetMax = element.placement.offsetMax,
                                 .size = element.size,
                                 .placedByLayout = byLayout};
}

bool finite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

Expected<GameUiPlacementResult, CommandError> gameUiPlace(CommandContext& context,
                                                          const GameUiPlaceParams& params)
{
    if (!finite(params.min))
    {
        return unexpected(CommandError::invalid("/min", "must be finite numbers"));
    }
    if (!finite(params.max))
    {
        return unexpected(CommandError::invalid("/max", "must be finite numbers"));
    }
    if (params.max.x < params.min.x || params.max.y < params.min.y)
    {
        return unexpected(CommandError::invalid("/max", "must not be below min on either axis"));
    }
    if (Expected<void, CommandError> valid = checkTarget(params.width, params.height); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Entity, CommandError> entity = uiElement(context, params.entity);
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<LaidOut, CommandError> laidOut = layOut(context, params.width, params.height);
    if (!laidOut)
    {
        return unexpected(std::move(laidOut).error());
    }
    World& world = context.world();
    const PlacedElement placed = placedElement(world, laidOut->layout, *entity);
    if (placed.laidOut == nullptr)
    {
        return unexpected(notLaidOut());
    }
    UiElement element = world.get<UiElement>(*entity);
    if (placed.byLayout)
    {
        element.size = params.max - params.min;
    }
    else
    {
        element.placement = placementFor(element.placement, placed.parent, UiRect{params.min, params.max});
    }
    return writeElement(context, *entity, element, placed.byLayout);
}

Expected<GameUiPlacementResult, CommandError> gameUiSetAnchors(CommandContext& context,
                                                               const GameUiSetAnchorsParams& params)
{
    UiAnchors anchors;
    if (params.preset)
    {
        if (params.anchorMin || params.anchorMax)
        {
            return unexpected(
                CommandError::invalid("/preset", "give either preset or anchorMin and anchorMax"));
        }
        if (Expected<void, CommandError> valid =
                checkEnum(*params.preset, UiAnchorPreset::Stretch, "/preset");
            !valid)
        {
            return unexpected(std::move(valid).error());
        }
        anchors = anchorsOf(*params.preset);
    }
    else
    {
        if (!params.anchorMin && !params.anchorMax)
        {
            return unexpected(CommandError::invalid("/preset", "give a preset or anchorMin and anchorMax"));
        }
        if (!params.anchorMin)
        {
            return unexpected(CommandError::invalid("/anchorMin", "is required with anchorMax"));
        }
        if (!params.anchorMax)
        {
            return unexpected(CommandError::invalid("/anchorMax", "is required with anchorMin"));
        }
        const auto inUnitRange = [](Vec2 value) {
            return finite(value) && value.x >= 0.0f && value.y >= 0.0f && value.x <= 1.0f && value.y <= 1.0f;
        };
        if (!inUnitRange(*params.anchorMin))
        {
            return unexpected(CommandError::invalid("/anchorMin", "must be between 0 and 1 on each axis"));
        }
        if (!inUnitRange(*params.anchorMax))
        {
            return unexpected(CommandError::invalid("/anchorMax", "must be between 0 and 1 on each axis"));
        }
        if (params.anchorMax->x < params.anchorMin->x || params.anchorMax->y < params.anchorMin->y)
        {
            return unexpected(
                CommandError::invalid("/anchorMax", "must not be below anchorMin on either axis"));
        }
        anchors = {.min = *params.anchorMin, .max = *params.anchorMax};
    }
    if (Expected<void, CommandError> valid = checkTarget(params.width, params.height); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Entity, CommandError> entity = uiElement(context, params.entity);
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    World& world = context.world();
    const bool keepRect = params.keepRect.value_or(true);
    const Entity parent = parentOf(world, *entity);
    if (!parent.isNull() && (world.has<UiStack>(parent) || world.has<UiGrid>(parent)))
    {
        return unexpected(
            blocked("the element is a child of a stack or grid, which places it; anchors do not "
                    "apply"));
    }
    UiElement element = world.get<UiElement>(*entity);
    UiRect parentRect;
    if (keepRect)
    {
        Expected<LaidOut, CommandError> laidOut = layOut(context, params.width, params.height);
        if (!laidOut)
        {
            return unexpected(std::move(laidOut).error());
        }
        const PlacedElement placed = placedElement(world, laidOut->layout, *entity);
        if (placed.laidOut == nullptr)
        {
            return unexpected(notLaidOut());
        }
        parentRect = placed.parent;
    }
    element.placement = withAnchors(element.placement, parentRect, anchors, keepRect);
    return writeElement(context, *entity, element, false);
}

} // namespace

void registerGameUiCommands(CommandRegistry& registry)
{
    registerUiComponents();
    registry.add<&gameUiCreate>({
        .name = "gameui.create",
        .title = strings::kCommandGameuiCreateTitle,
        .description = strings::kCommandGameuiCreateDescription,
        .agentSummary =
            "Creates a game UI canvas or element, with the children its kind needs, as one undo step. Kinds: "
            "canvas (UiCanvas), panel (translucent UiImage, 400x300), text (UiText, 320x48, content-sized "
            "in a stack), button (240x64, UiButton with a centred child \"label\"), toggle (48x48, child "
            "\"check\" shown while on), slider (320x24, children \"fill\" and \"handle\"), text_field "
            "(320x48), scroll_view (400x300, child \"content\" with a UiStack), stack (320x400) and grid "
            "(440x440). Elements are placed at (24, 24) from the top left of the parent, in canvas units. "
            "parent defaults to the selected canvas or UI element, else the first canvas; one named "
            "\"Canvas\" is created (and returned as canvas) when the scene has none. text is the text of a "
            "text, a button's label or a text field's placeholder; texts use the project's first font. "
            "Example: {\"kind\": \"button\", \"parent\": \"/HUD\", \"name\": \"Start\", \"text\": "
            "\"Start\"}.",
        .category = "Game UI",
        .icon = "dashboard_customize",
        .flags = {.undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&gameUiLayout>({
        .name = "gameui.layout",
        .title = strings::kCommandGameuiLayoutTitle,
        .description = strings::kCommandGameuiLayoutDescription,
        .agentSummary =
            "Returns where the game UI places its elements, in draw order: per element its rectangle in "
            "canvas units (min, max; y down from the top left) and its bounds in target pixels, opacity, "
            "visibility, whether it is interactable and focused, plus each canvas's pixelsPerUnit and size. "
            "The edit World is laid out for width x height pixels (default 1920x1080) without changing the "
            "scene; while playing it reads the play World as its last update laid it out (the Game view's "
            "size; 1920x1080 headless), unless world is \"edit\". entity limits it to one canvas or "
            "element and its subtree. Example: {\"entity\": \"/HUD\", \"width\": 1280, \"height\": 720}.",
        .category = "Game UI",
        .icon = "view_quilt",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&gameUiClick>({
        .name = "gameui.click",
        .title = strings::kCommandGameuiClickTitle,
        .description = strings::kCommandGameuiClickDescription,
        .agentSummary =
            "Play mode: clicks an element of the running game's UI at its centre, as the mouse would. It "
            "runs two updates of the game, paused or not (the pointer held there, then released), so "
            "buttons report clicked, toggles flip and sliders move to the value at their centre, and "
            "returns the UI events of those updates. Fails, saying why, when the element is hidden, "
            "disabled, not interactable, below a modal canvas, outside a clip or covered by another "
            "element. The game's own input actions do not see the click. Example: {\"entity\": "
            "\"/Menu/Start\"}.",
        .category = "Game UI",
        .icon = "ads_click",
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
    });
    registry.add<&gameUiFocus>({
        .name = "gameui.focus",
        .title = strings::kCommandGameuiFocusTitle,
        .description = strings::kCommandGameuiFocusDescription,
        .agentSummary =
            "Play mode: gives an element of the running game's UI the focus of navigation (no event), or "
            "clears it with null, and returns the focused element. An element that cannot take the focus "
            "(not laid out, hidden, disabled or not focusable) leaves it as it was, with a warning. Example: "
            "{\"entity\": \"/Menu/Start\"}.",
        .category = "Game UI",
        .icon = "center_focus_weak",
        .flags = {.idempotent = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
    });
    registry.add<&gameUiPick>({
        .name = "gameui.pick",
        .title = strings::kCommandGameuiPickTitle,
        .description = strings::kCommandGameuiPickDescription,
        .agentSummary =
            "Returns the topmost game UI element at a pixel of the UI target (x, y from the top left), "
            "interactable or not: the last one in draw order that is visible and contains the pixel after "
            "its "
            "rotation and scale, inside the clips of its ancestors; canvases higher in order come first. "
            "path "
            "lists its canvas, then its ancestors down to it. The edit World is laid out for width x height "
            "pixels (default 1920x1080); while playing it reads the play World as its last update laid it "
            "out. "
            "entity is absent when nothing is there. Example: {\"x\": 960, \"y\": 540, \"width\": 1920, "
            "\"height\": 1080}.",
        .category = "Game UI",
        .icon = "arrow_selector_tool",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&gameUiPlace>({
        .name = "gameui.place",
        .title = strings::kCommandGameuiPlaceTitle,
        .description = strings::kCommandGameuiPlaceDescription,
        .agentSummary =
            "Moves and resizes a game UI element so that its rectangle at a target size is [min, max] in "
            "canvas "
            "units (y down from the top left of the canvas, as gameui.layout reports it), as one undo step. "
            "It "
            "writes the offsets of illusion.UiElement placement and keeps the anchors, so with anchors that "
            "stretch the rectangle is exact only at that size (default 1920x1080 pixels); set coinciding "
            "anchors first (gameui.set_anchors) for a fixed size. A child of a UiStack or UiGrid is placed "
            "by "
            "its parent: only its size (max - min) is written and placedByLayout is true. Fails for an "
            "element "
            "that is not laid out (inactive, or not under an active canvas). Example: {\"entity\": "
            "\"/HUD/Start\", \"min\": [100, 50], \"max\": [340, 114]}.",
        .category = "Game UI",
        .icon = "open_with",
        .flags = {.undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&gameUiSetAnchors>({
        .name = "gameui.set_anchors",
        .title = strings::kCommandGameuiSetAnchorsTitle,
        .description = strings::kCommandGameuiSetAnchorsDescription,
        .agentSummary =
            "Sets the anchors of a game UI element (illusion.UiElement placement), as one undo step: either "
            "a "
            "preset (top_left, top, top_right, left, center, right, bottom_left, bottom, bottom_right, "
            "stretch_top, stretch_middle, stretch_bottom, stretch_left, stretch_center, stretch_right, "
            "stretch) "
            "or anchorMin and anchorMax, normalised to the parent (0 = left/top, 1 = right/bottom). With "
            "keepRect (default true) the offsets change so that the element keeps its rectangle at the "
            "target "
            "size (width x height, default 1920x1080); without it the offsets stay and the element moves "
            "with "
            "its anchors. Refused for a child of a UiStack or UiGrid, which its parent places. Example: "
            "{\"entity\": \"/HUD/Score\", \"preset\": \"top_right\"}.",
        .category = "Game UI",
        .icon = "anchor",
        .flags = {.undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail
