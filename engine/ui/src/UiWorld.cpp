#include <illusion/assets/Font.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>
#include <vector>

namespace illusion
{
namespace
{

constexpr u32 kNoNode = ~0u;
constexpr u32 kReservedCanvases = 64;
// Formatted texts of UiText keys, rebuilt with the layout.
constexpr usize kTextArenaBytes = usize{256} * 1024;
// Marks a main-axis size of a UiStack child that stretches.
constexpr f32 kStretch = -1.0f;
// Navigation: deflection that counts as a direction, and the repeat while it is held, in seconds.
constexpr f32 kNavigateThreshold = 0.5f;
constexpr f64 kRepeatDelay = 0.4;
constexpr f64 kRepeatInterval = 0.12;
// Navigation scores a candidate by its distance along the direction plus this times its offset across it.
constexpr f32 kCrossPenalty = 2.0f;
// Candidates closer than this along the direction, in pixels, are not in that direction.
constexpr f32 kMinAdvancePixels = 0.5f;

enum class Direction : u8
{
    None,
    Up,
    Down,
    Left,
    Right,
};

struct Node
{
    Entity entity{};
    u32 parent = kNoNode;
    u32 firstChild = kNoNode;
    u32 lastChild = kNoNode;
    u32 nextSibling = kNoNode;
    u32 childCount = 0;
    u32 subtreeEnd = 0; // one past the last descendant's node
    UiRect rect{};
    UiTransform2D transform{};
    f32 opacity = 1.0f;
    std::string_view keyText{}; // the formatted UiText::key, in the text arena
    bool keyed = false;
    bool element = false; // has UiElement
    bool clip = false;
    bool interactable = false; // has UiInteractable
    bool toggle = false;       // has UiToggle
    bool slider = false;       // has UiSlider
    bool indicator = false;    // first child element of a UiToggle, hidden while it is off
};

struct CanvasEntry
{
    Entity entity{};
    i32 order = 0;
    u32 firstNode = 0;
    u32 endNode = 0;
    UiCanvasMetrics metrics{};
    bool modal = false;
};

// A clip on the path to the node a hit test visits.
struct HitClip
{
    u32 end = 0; // subtreeEnd of the node that clips
    bool inside = false;
};

// What a UiStack child measures: its text or image size, when it has either.
struct Content
{
    bool known = false;
    Vec2 size{0.0f};
};

// The Component versions a layout depends on.
constexpr usize kVersionCount = 11;
using Versions = std::array<u64, kVersionCount>;

Versions versionsOf(const World& world) noexcept
{
    return {world.componentVersion<UiCanvas>(),       world.componentVersion<UiElement>(),
            world.componentVersion<UiImage>(),        world.componentVersion<UiText>(),
            world.componentVersion<UiStack>(),        world.componentVersion<UiGrid>(),
            world.componentVersion<Hierarchy>(),      world.componentVersion<Inactive>(),
            world.componentVersion<UiInteractable>(), world.componentVersion<UiToggle>(),
            world.componentVersion<UiSlider>()};
}

// @p point in the space of @p transform's input, or nothing when the transform collapses an axis.
bool untransform(const UiTransform2D& transform, Vec2 point, Vec2& local) noexcept
{
    const f32 det = transform.x.x * transform.y.y - transform.y.x * transform.x.y;
    if (!(std::abs(det) > 1e-12f))
    {
        return false;
    }
    const Vec2 d = point - transform.translation;
    local = Vec2((d.x * transform.y.y - d.y * transform.y.x) / det,
                 (d.y * transform.x.x - d.x * transform.x.y) / det);
    return true;
}

// Where the value of @p slider is between min and max, 0 to 1.
f32 sliderFraction(const UiSlider& slider) noexcept
{
    const f32 range = slider.max - slider.min;
    if (!std::isfinite(range) || range == 0.0f || !std::isfinite(slider.value))
    {
        return 0.0f;
    }
    return std::clamp((slider.value - slider.min) / range, 0.0f, 1.0f);
}

// @p value snapped to the step of @p slider from its min and kept between min and max.
f32 sliderValue(const UiSlider& slider, f32 value) noexcept
{
    const f32 low = std::min(slider.min, slider.max);
    const f32 high = std::max(slider.min, slider.max);
    if (!std::isfinite(value))
    {
        return low;
    }
    if (slider.step > 0.0f && std::isfinite(slider.step))
    {
        value = slider.min + std::round((value - slider.min) / slider.step) * slider.step;
    }
    return std::clamp(value, low, high);
}

Vec4 stateTint(const UiInteractable& interactable, const UiInteraction& state) noexcept
{
    if (!interactable.enabled)
    {
        return interactable.disabledTint;
    }
    if (state.pressed)
    {
        return interactable.pressedTint;
    }
    if (state.focused)
    {
        return interactable.focusedTint;
    }
    return state.hovered ? interactable.hoveredTint : Vec4(1.0f);
}

bool sameState(const UiInteraction& a, const UiInteraction& b) noexcept
{
    return a.hovered == b.hovered && a.pressed == b.pressed && a.focused == b.focused;
}

// The direction of @p navigate beyond the threshold, the larger axis winning.
Direction directionOf(Vec2 navigate) noexcept
{
    const f32 x = std::isfinite(navigate.x) ? navigate.x : 0.0f;
    const f32 y = std::isfinite(navigate.y) ? navigate.y : 0.0f;
    if (std::max(std::abs(x), std::abs(y)) <= kNavigateThreshold)
    {
        return Direction::None;
    }
    if (std::abs(x) >= std::abs(y))
    {
        return x > 0.0f ? Direction::Right : Direction::Left;
    }
    return y > 0.0f ? Direction::Up : Direction::Down;
}

// The direction on the canvas, y down.
Vec2 canvasDirection(Direction direction) noexcept
{
    switch (direction)
    {
    case Direction::Up:
        return {0.0f, -1.0f};
    case Direction::Down:
        return {0.0f, 1.0f};
    case Direction::Left:
        return {-1.0f, 0.0f};
    case Direction::Right:
        return {1.0f, 0.0f};
    case Direction::None:
        break;
    }
    return {0.0f, 0.0f};
}

Entity neighbourOf(const UiInteractable& interactable, Direction direction) noexcept
{
    switch (direction)
    {
    case Direction::Up:
        return interactable.up;
    case Direction::Down:
        return interactable.down;
    case Direction::Left:
        return interactable.left;
    case Direction::Right:
        return interactable.right;
    case Direction::None:
        break;
    }
    return {};
}

UiTextStyle textStyle(const UiText& text, f32 wrapWidth) noexcept
{
    UiTextStyle style;
    style.size = text.size;
    style.color = text.color;
    style.align = text.align;
    style.lineSpacing = text.lineSpacing;
    style.letterSpacing = text.letterSpacing;
    style.wrapWidth = text.wrap ? std::max(wrapWidth, 0.0f) : 0.0f;
    return style;
}

UiTransform2D scaled(const UiTransform2D& transform, f32 factor) noexcept
{
    return {transform.x * factor, transform.y * factor, transform.translation * factor};
}

bool axisAligned(const UiTransform2D& transform) noexcept
{
    return transform.x.y == 0.0f && transform.y.x == 0.0f;
}

bool hasBorder(const UiEdges& edges) noexcept
{
    return edges.left > 0.0f || edges.top > 0.0f || edges.right > 0.0f || edges.bottom > 0.0f;
}

// The largest rectangle of @p imageSize's aspect centred in @p rect.
UiRect fitted(const UiRect& rect, Vec2 imageSize) noexcept
{
    const Vec2 size = rect.size();
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f || size.x <= 0.0f || size.y <= 0.0f)
    {
        return rect;
    }
    const f32 scale = std::min(size.x / imageSize.x, size.y / imageSize.y);
    const Vec2 shown = imageSize * scale;
    const Vec2 min = rect.min + (size - shown) * 0.5f;
    return {min, min + shown};
}

} // namespace

struct UiWorld::Impl
{
    Impl(World& targetWorld, const UiWorldDesc& worldDesc)
        : world(targetWorld)
        , desc(worldDesc)
        , drawList(worldDesc.drawList)
        , textArena(kTextArenaBytes)
    {
        nodes.reserve(usize{desc.maxElements} + kReservedCanvases);
        canvases.reserve(kReservedCanvases);
        depthStack.resize(usize{desc.maxDepth} + 1);
        clipStack.reserve(usize{desc.drawList.maxClipDepth} + 1);
        hitClips.reserve(usize{desc.maxDepth} + 1);
        events.reserve(desc.maxEvents);
    }

    [[nodiscard]] std::string_view textOf(const Node& node, const UiText& text) const noexcept
    {
        return node.keyed ? node.keyText : std::string_view(text.text);
    }

    [[nodiscard]] bool changed(const UiFrame& frame, const Versions& now) const noexcept
    {
        if (!laidOut || pendingContent || now != versions || frame.targetPixels != lastFrame.targetPixels ||
            !(frame.safeInsetPixels == lastFrame.safeInsetPixels) || assets != lastAssets ||
            localization != lastLocalization)
        {
            return true;
        }
        return localization != nullptr && !(localization->locale() == lastLocale);
    }

    void update(const UiFrame& frame, const UiInput& input)
    {
        const bool rebuilt = hierarchy.update(world);
        const Versions now = versionsOf(world);
        statistics.layoutReused = !rebuilt && !changed(frame, now);
        if (!statistics.layoutReused)
        {
            layout(frame);
            lastFrame = frame;
            lastAssets = assets;
            lastLocalization = localization;
            lastLocale = localization != nullptr ? localization->locale() : text::LocaleId{};
            laidOut = true;
        }
        interact(input);
        // The layout now matches the Components, including the values interact() wrote.
        versions = versionsOf(world);
        record(frame);
        statistics.droppedEvents = droppedEvents;
    }

    void layout(const UiFrame& frame)
    {
        ILL_PROFILE_SCOPE_N("UiWorld::layout");
        nodes.clear();
        canvases.clear();
        textArena.reset();
        pendingContent = false;
        elements = 0;
        skipped = 0;
        missingKeys = 0;
        world.query<const UiCanvas>().without<Inactive>().each([this](Entity entity, const UiCanvas& canvas) {
            canvases.push_back({.entity = entity,
                                .order = canvas.order,
                                .firstNode = 0,
                                .endNode = 0,
                                .metrics = {},
                                .modal = canvas.modal});
        });
        std::sort(canvases.begin(), canvases.end(), [](const CanvasEntry& a, const CanvasEntry& b) {
            return a.order != b.order ? a.order < b.order : a.entity.index < b.entity.index;
        });
        firstEligible = 0;
        modal = false;
        for (u32 c = 0; c < canvases.size(); ++c)
        {
            CanvasEntry& canvas = canvases[c];
            const UiCanvas& settings = world.get<UiCanvas>(canvas.entity);
            canvas.metrics = canvasMetrics(settings.scale, frame.targetPixels, frame.safeInsetPixels);
            const UiRect area =
                settings.useSafeArea ? canvas.metrics.safeArea : UiRect{Vec2(0.0f), canvas.metrics.size};
            canvas.firstNode = static_cast<u32>(nodes.size());
            buildNodes(canvas.entity, area);
            canvas.endNode = static_cast<u32>(nodes.size());
            for (u32 n = canvas.firstNode; n < canvas.endNode; ++n)
            {
                finishNode(n);
                layoutChildren(n);
            }
            if (canvas.modal && canvas.endNode > canvas.firstNode + 1)
            {
                firstEligible = c;
                modal = true;
            }
        }
        // Elements that are no longer laid out keep no state; the laid-out ones get theirs in interact().
        world.query<UiInteraction>().each([](Entity, UiInteraction& state) { state = {}; });
        world.markChanged<UiInteraction>();
        writeComputedRects(0, static_cast<u32>(nodes.size()));
    }

    u32 addNode(Entity entity, u32 parent)
    {
        const u32 id = static_cast<u32>(nodes.size());
        Node& node = nodes.emplace_back();
        node.entity = entity;
        node.parent = parent;
        node.element = world.has<UiElement>(entity);
        if (node.element)
        {
            node.interactable = world.has<UiInteractable>(entity);
            node.toggle = world.has<UiToggle>(entity);
            node.slider = world.has<UiSlider>(entity);
        }
        if (parent != kNoNode)
        {
            Node& parentNode = nodes[parent];
            if (parentNode.lastChild == kNoNode)
            {
                parentNode.firstChild = id;
                node.indicator = parentNode.toggle;
            }
            else
            {
                nodes[parentNode.lastChild].nextSibling = id;
            }
            parentNode.lastChild = id;
            ++parentNode.childCount;
        }
        resolveKey(node);
        return id;
    }

    void resolveKey(Node& node)
    {
        const UiText* text = world.tryGet<UiText>(node.entity);
        if (text == nullptr || text->key.empty())
        {
            return;
        }
        node.keyed = true;
        text::TextOutput out(textArena);
        const text::MessageFormat* message =
            localization != nullptr ? localization->find(text->key) : nullptr;
        if (message != nullptr)
        {
            (void)message->format(std::span<const text::MessageArgument>(), localization->context(), out);
        }
        else
        {
            ++missingKeys;
            out.append(text->key);
        }
        node.keyText = out.view();
    }

    // Adds the canvas and its descendants with UiElement as nodes, in pre-order.
    void buildNodes(Entity canvas, const UiRect& area)
    {
        const u32 root = addNode(canvas, kNoNode);
        nodes[root].rect = area;
        depthStack[0] = root;
        u32 top = 0;
        const std::span<const Entity> subtree = hierarchy.subtree(canvas);
        const u32 base = subtree.empty() ? 0 : hierarchy.depth(canvas);
        u32 skipDepth = kNoNode;
        bool countSkipped = false;
        for (usize i = 1; i < subtree.size(); ++i)
        {
            const Entity entity = subtree[i];
            const u32 depth = hierarchy.depth(entity) - base;
            if (skipDepth != kNoNode)
            {
                if (depth > skipDepth)
                {
                    if (countSkipped && world.has<UiElement>(entity))
                    {
                        ++skipped;
                    }
                    continue;
                }
                skipDepth = kNoNode;
            }
            if (world.has<Inactive>(entity) || world.has<UiCanvas>(entity) || !world.has<UiElement>(entity))
            {
                skipDepth = depth;
                countSkipped = false;
                continue;
            }
            if (depth > desc.maxDepth || elements >= desc.maxElements)
            {
                ++skipped;
                skipDepth = depth;
                countSkipped = true;
                continue;
            }
            const u32 id = static_cast<u32>(nodes.size());
            for (u32 level = depth; level <= top; ++level)
            {
                nodes[depthStack[level]].subtreeEnd = id;
            }
            (void)addNode(entity, depthStack[depth - 1]);
            depthStack[depth] = id;
            top = depth;
            ++elements;
        }
        for (u32 level = 0; level <= top; ++level)
        {
            nodes[depthStack[level]].subtreeEnd = static_cast<u32>(nodes.size());
        }
    }

    // Accumulates the transform and opacity of node @p n, whose rectangle is placed.
    void finishNode(u32 n)
    {
        Node& node = nodes[n];
        const UiTransform2D parentTransform =
            node.parent != kNoNode ? nodes[node.parent].transform : UiTransform2D{};
        const f32 parentOpacity = node.parent != kNoNode ? nodes[node.parent].opacity : 1.0f;
        node.transform = parentTransform;
        node.opacity = parentOpacity;
        if (const UiElement* element = node.element ? world.tryGet<UiElement>(node.entity) : nullptr)
        {
            if (element->rotation != 0.0f || element->scale != Vec2(1.0f))
            {
                const Vec2 pivot = node.rect.min + element->pivot * node.rect.size();
                node.transform =
                    combine(parentTransform, makeUiTransform(pivot, element->rotation, element->scale));
            }
            node.opacity = parentOpacity * std::clamp(element->opacity, 0.0f, 1.0f);
            node.clip = element->clipChildren;
        }
    }

    [[nodiscard]] Content contentOf(const Node& node, f32 wrapWidth)
    {
        if (const UiText* text = world.tryGet<UiText>(node.entity))
        {
            const UiFontFace face = assets != nullptr ? assets->font(text->font) : UiFontFace{};
            if (face.font == nullptr)
            {
                pendingContent = pendingContent || (assets != nullptr && !text->font.empty());
                return {true, Vec2(0.0f)};
            }
            return {true, measureText(*face.font, textOf(node, *text), textStyle(*text, wrapWidth)).size};
        }
        if (const UiImage* image = world.tryGet<UiImage>(node.entity);
            image != nullptr && !image->image.empty())
        {
            const UiImageSource source = assets != nullptr ? assets->image(image->image) : UiImageSource{};
            if (source.texture.value == 0)
            {
                pendingContent = pendingContent || assets != nullptr;
                return {true, Vec2(0.0f)};
            }
            return {true, source.size};
        }
        return {};
    }

    void layoutChildren(u32 n)
    {
        const Node& node = nodes[n];
        if (node.childCount == 0)
        {
            return;
        }
        if (const UiStack* stack = world.tryGet<UiStack>(node.entity))
        {
            layoutStack(n, *stack);
        }
        else if (const UiGrid* grid = world.tryGet<UiGrid>(node.entity))
        {
            layoutGrid(n, *grid);
        }
        else
        {
            const UiRect parent = node.rect;
            for (u32 c = node.firstChild; c != kNoNode; c = nodes[c].nextSibling)
            {
                nodes[c].rect = resolvePlacement(world.get<UiElement>(nodes[c].entity).placement, parent);
            }
        }
        if (nodes[n].slider)
        {
            placeSliderParts(n);
        }
    }

    // The fill from the slider's start to its value, and the handle centred on the value along its axis.
    void placeSliderParts(u32 n)
    {
        const Node& node = nodes[n];
        const UiSlider& slider = world.get<UiSlider>(node.entity);
        const int along = slider.axis == UiAxis::Vertical ? 1 : 0;
        const f32 at = node.rect.min[along] + sliderFraction(slider) * node.rect.size()[along];
        const u32 fill = node.firstChild;
        if (fill == kNoNode)
        {
            return;
        }
        nodes[fill].rect = node.rect;
        nodes[fill].rect.max[along] = at;
        const u32 handle = nodes[fill].nextSibling;
        if (handle == kNoNode)
        {
            return;
        }
        UiRect& rect = nodes[handle].rect;
        const f32 half = rect.size()[along] * 0.5f;
        rect.min[along] = at - half;
        rect.max[along] = at + half;
    }

    // Lays out the subtree of slider node @p n again after its value changed.
    void relayoutSlider(u32 n)
    {
        layoutChildren(n);
        const u32 end = nodes[n].subtreeEnd;
        for (u32 m = n + 1; m < end; ++m)
        {
            finishNode(m);
            layoutChildren(m);
        }
        writeComputedRects(n + 1, end);
    }

    void layoutStack(u32 n, const UiStack& stack)
    {
        const UiRect inner = inset(nodes[n].rect, stack.padding);
        const bool vertical = stack.axis == UiAxis::Vertical;
        const int along = vertical ? 1 : 0;
        const int across = 1 - along;
        const Vec2 innerSize = inner.size();
        const f32 spacing = std::max(stack.spacing, 0.0f);
        f32 used = 0.0f;
        u32 stretched = 0;
        // First pass: each child's size along and across the stack (kStretch along shares the rest), kept in
        // rect.min.
        for (u32 c = nodes[n].firstChild; c != kNoNode; c = nodes[c].nextSibling)
        {
            const Vec2 fixed = glm::max(world.get<UiElement>(nodes[c].entity).size, Vec2(0.0f));
            const f32 wrapAt =
                vertical
                    ? (stack.crossAlign != UiCrossAlign::Stretch && fixed.x > 0.0f ? fixed.x : innerSize.x)
                    : (fixed.x > 0.0f ? fixed.x : innerSize.x);
            const Content content = contentOf(nodes[c], wrapAt);
            Vec2 size(0.0f);
            size[along] = fixed[along] > 0.0f ? fixed[along] : content.known ? content.size[along] : kStretch;
            size[across] = stack.crossAlign == UiCrossAlign::Stretch ? innerSize[across]
                           : fixed[across] > 0.0f                    ? fixed[across]
                           : content.known                           ? content.size[across]
                                                                     : innerSize[across];
            if (size[along] == kStretch)
            {
                ++stretched;
            }
            else
            {
                used += size[along];
            }
            nodes[c].rect.min = size;
        }
        const f32 gaps = spacing * static_cast<f32>(nodes[n].childCount - 1);
        const f32 share = stretched > 0
                              ? std::max(innerSize[along] - used - gaps, 0.0f) / static_cast<f32>(stretched)
                              : 0.0f;
        f32 cursor = inner.min[along];
        for (u32 c = nodes[n].firstChild; c != kNoNode; c = nodes[c].nextSibling)
        {
            Vec2 size = nodes[c].rect.min;
            if (size[along] == kStretch)
            {
                size[along] = share;
            }
            Vec2 min(0.0f);
            min[along] = cursor;
            switch (stack.crossAlign)
            {
            case UiCrossAlign::Center:
                min[across] = inner.min[across] + (innerSize[across] - size[across]) * 0.5f;
                break;
            case UiCrossAlign::End:
                min[across] = inner.max[across] - size[across];
                break;
            case UiCrossAlign::Start:
            case UiCrossAlign::Stretch:
            default:
                min[across] = inner.min[across];
                break;
            }
            nodes[c].rect = {min, min + size};
            cursor += size[along] + spacing;
        }
    }

    void layoutGrid(u32 n, const UiGrid& grid)
    {
        const UiRect inner = inset(nodes[n].rect, grid.padding);
        const u32 columns = std::max(grid.columns, 1u);
        const Vec2 cell = glm::max(grid.cellSize, Vec2(0.0f));
        const Vec2 pitch = cell + glm::max(grid.spacing, Vec2(0.0f));
        u32 index = 0;
        for (u32 c = nodes[n].firstChild; c != kNoNode; c = nodes[c].nextSibling)
        {
            const Vec2 fixed = glm::max(world.get<UiElement>(nodes[c].entity).size, Vec2(0.0f));
            const Vec2 size(fixed.x > 0.0f ? fixed.x : cell.x, fixed.y > 0.0f ? fixed.y : cell.y);
            const Vec2 cellMin = inner.min + pitch * Vec2(static_cast<f32>(index % columns),
                                                          static_cast<f32>(index / columns));
            const Vec2 min = cellMin + (cell - size) * 0.5f;
            nodes[c].rect = {min, min + size};
            ++index;
        }
    }

    // Writes UiComputedRect to the elements of nodes [first, end), and gives the interactable ones a
    // UiInteraction.
    void writeComputedRects(u32 first, u32 end)
    {
        bool written = false;
        for (u32 n = first; n < end; ++n)
        {
            const Node& node = nodes[n];
            if (!node.element)
            {
                continue;
            }
            const UiComputedRect computed{node.rect, node.transform, node.opacity};
            if (UiComputedRect* existing = world.tryGet<UiComputedRect>(node.entity))
            {
                *existing = computed;
            }
            else
            {
                world.emplace<UiComputedRect>(node.entity, computed);
            }
            if (node.interactable && !world.has<UiInteraction>(node.entity))
            {
                world.emplace<UiInteraction>(node.entity);
            }
            written = true;
        }
        if (written)
        {
            world.markChanged<UiComputedRect>();
        }
    }

    // The clip rectangle node @p node pushes for its descendants, in the units of the draw list.
    [[nodiscard]] static UiRect clipOf(const Node& node, f32 factor) noexcept
    {
        UiRect rect = node.rect;
        if (axisAligned(node.transform))
        {
            const Vec2 a = node.transform.apply(rect.min);
            const Vec2 b = node.transform.apply(rect.max);
            rect = {glm::min(a, b), glm::max(a, b)};
        }
        return {rect.min * factor, rect.max * factor};
    }

    void drawNode(const Node& node)
    {
        if (node.opacity <= 0.0f)
        {
            return;
        }
        if (const UiImage* image = world.tryGet<UiImage>(node.entity))
        {
            Vec4 tint = image->tint;
            if (const UiInteractable* interactable =
                    node.interactable ? world.tryGet<UiInteractable>(node.entity) : nullptr)
            {
                tint *= stateTint(*interactable, stateOf(node.entity));
            }
            tint.a *= node.opacity;
            if (image->image.empty())
            {
                (void)drawList.rect(node.rect, tint);
            }
            else if (const UiImageSource source =
                         assets != nullptr ? assets->image(image->image) : UiImageSource{};
                     source.texture.value != 0)
            {
                if (image->preserveAspect)
                {
                    (void)drawList.image(fitted(node.rect, source.size), source.texture, tint);
                }
                else if (hasBorder(image->border))
                {
                    (void)drawList.nineSlice(node.rect, source.texture, source.size, image->border, tint,
                                             image->borderScale);
                }
                else
                {
                    (void)drawList.image(node.rect, source.texture, tint);
                }
            }
        }
        const UiText* text = world.tryGet<UiText>(node.entity);
        if (text == nullptr || assets == nullptr)
        {
            return;
        }
        const UiFontFace face = assets->font(text->font);
        if (face.font == nullptr)
        {
            return;
        }
        UiTextStyle style = textStyle(*text, node.rect.size().x);
        style.color.a *= node.opacity;
        const std::string_view utf8 = textOf(node, *text);
        Vec2 position = node.rect.min;
        if (style.wrapWidth <= 0.0f)
        {
            if (text->align == UiTextAlign::Center)
            {
                position.x = (node.rect.min.x + node.rect.max.x) * 0.5f;
            }
            else if (text->align == UiTextAlign::End)
            {
                position.x = node.rect.max.x;
            }
        }
        if (text->verticalAlign != UiVerticalAlign::Top)
        {
            const f32 height = measureText(*face.font, utf8, style).size.y;
            position.y = text->verticalAlign == UiVerticalAlign::Middle
                             ? node.rect.min.y + (node.rect.size().y - height) * 0.5f
                             : node.rect.max.y - height;
        }
        (void)drawList.text(face, position, utf8, style);
    }

    [[nodiscard]] bool hiddenByToggle(const Node& node) const
    {
        if (!node.indicator)
        {
            return false;
        }
        const Entity parent = nodes[node.parent].entity;
        const UiToggle* toggle = world.isAlive(parent) ? world.tryGet<UiToggle>(parent) : nullptr;
        return toggle != nullptr && !toggle->on;
    }

    // True when node @p n or one of its ancestors is hidden by a toggle.
    [[nodiscard]] bool hidden(u32 n) const
    {
        for (u32 m = n; m != kNoNode; m = nodes[m].parent)
        {
            if (hiddenByToggle(nodes[m]))
            {
                return true;
            }
        }
        return false;
    }

    // First node of the canvases that can be hit and focused: the topmost modal one and those above it.
    [[nodiscard]] u32 eligibleBegin() const noexcept
    {
        return canvases.empty() ? 0 : canvases[firstEligible].firstNode;
    }

    [[nodiscard]] u32 canvasOf(u32 n) const noexcept
    {
        for (u32 c = 0; c < canvases.size(); ++c)
        {
            if (n < canvases[c].endNode)
            {
                return c;
            }
        }
        return 0;
    }

    // The node of @p entity on the canvases that can be focused, or kNoNode; @p hint is tried first.
    [[nodiscard]] u32 findNode(Entity entity, u32 hint) const noexcept
    {
        if (entity.isNull() || !world.isAlive(entity))
        {
            return kNoNode;
        }
        const u32 begin = eligibleBegin();
        if (hint != kNoNode && hint >= begin && hint < nodes.size() && nodes[hint].entity == entity)
        {
            return hint;
        }
        for (u32 n = begin; n < nodes.size(); ++n)
        {
            if (nodes[n].entity == entity)
            {
                return n;
            }
        }
        return kNoNode;
    }

    [[nodiscard]] const UiInteractable* enabledInteractable(u32 n) const
    {
        if (!nodes[n].interactable)
        {
            return nullptr;
        }
        const UiInteractable* interactable = world.tryGet<UiInteractable>(nodes[n].entity);
        return interactable != nullptr && interactable->enabled ? interactable : nullptr;
    }

    [[nodiscard]] bool focusable(u32 n) const
    {
        const UiInteractable* interactable = enabledInteractable(n);
        return interactable != nullptr && interactable->focusable && !hidden(n);
    }

    // The node of @p entity when it can have the focus, or kNoNode.
    [[nodiscard]] u32 focusableNode(Entity entity, u32 hint) const
    {
        const u32 n = findNode(entity, hint);
        return n != kNoNode && focusable(n) ? n : kNoNode;
    }

    // Centre of node @p n after its transform, in target pixels.
    [[nodiscard]] Vec2 centreOf(u32 n) const noexcept
    {
        const Node& node = nodes[n];
        return node.transform.apply((node.rect.min + node.rect.max) * 0.5f) *
               canvases[canvasOf(n)].metrics.pixelsPerUnit;
    }

    [[nodiscard]] UiInteraction stateOf(Entity entity) const noexcept
    {
        UiInteraction state;
        state.hovered = entity == hovered;
        state.focused = entity == focus;
        const bool capturedSlider = capturedHint != kNoNode && nodes[capturedHint].slider;
        state.pressed = (entity == captured && (entity == hovered || capturedSlider)) ||
                        (entity == submitted && entity == focus);
        return state;
    }

    void push(UiEventType type, Entity entity, f32 value)
    {
        if (events.size() < desc.maxEvents)
        {
            events.push_back({type, entity, value});
        }
        else
        {
            ++droppedEvents;
        }
    }

    void focusNode(u32 n, f32 byNavigation)
    {
        focusHint = n;
        if (focus == nodes[n].entity)
        {
            return;
        }
        focus = nodes[n].entity;
        push(UiEventType::FocusChanged, focus, byNavigation);
    }

    // The topmost enabled interactable node under @p pixels, or kNoNode.
    [[nodiscard]] u32 hitTest(Vec2 pixels)
    {
        u32 hit = kNoNode;
        for (u32 c = firstEligible; c < canvases.size(); ++c)
        {
            const CanvasEntry& canvas = canvases[c];
            const Vec2 point = pixels / canvas.metrics.pixelsPerUnit;
            hitClips.clear();
            u32 outside = 0;
            u32 hiddenEnd = 0;
            for (u32 n = canvas.firstNode; n < canvas.endNode; ++n)
            {
                while (!hitClips.empty() && hitClips.back().end <= n)
                {
                    outside -= hitClips.back().inside ? 0u : 1u;
                    hitClips.pop_back();
                }
                const Node& node = nodes[n];
                if (n < hiddenEnd || hiddenByToggle(node))
                {
                    hiddenEnd = std::max(hiddenEnd, node.subtreeEnd);
                    continue;
                }
                Vec2 local(0.0f);
                if (outside == 0 && node.opacity > 0.0f && enabledInteractable(n) != nullptr &&
                    untransform(node.transform, point, local) && node.rect.contains(local))
                {
                    hit = n;
                }
                if (node.clip && node.subtreeEnd > n + 1)
                {
                    const bool inside = clipOf(node, 1.0f).contains(point);
                    hitClips.push_back({node.subtreeEnd, inside});
                    outside += inside ? 0u : 1u;
                }
            }
        }
        return hit;
    }

    void setSliderValue(u32 n, f32 value)
    {
        const Entity entity = nodes[n].entity;
        UiSlider* slider = world.tryGet<UiSlider>(entity);
        if (slider == nullptr)
        {
            return;
        }
        const f32 next = sliderValue(*slider, value);
        if (next == slider->value)
        {
            return;
        }
        slider->value = next;
        world.markChanged<UiSlider>();
        push(UiEventType::ValueChanged, entity, next);
        relayoutSlider(n);
    }

    void dragSlider(u32 n, Vec2 pixels)
    {
        const Node& node = nodes[n];
        const UiSlider* slider = world.tryGet<UiSlider>(node.entity);
        Vec2 local(0.0f);
        if (slider == nullptr ||
            !untransform(node.transform, pixels / canvases[canvasOf(n)].metrics.pixelsPerUnit, local))
        {
            return;
        }
        const int along = slider->axis == UiAxis::Vertical ? 1 : 0;
        const f32 length = node.rect.size()[along];
        const f32 t =
            length > 0.0f ? std::clamp((local[along] - node.rect.min[along]) / length, 0.0f, 1.0f) : 0.0f;
        setSliderValue(n, slider->min + t * (slider->max - slider->min));
    }

    // A button clicks, a toggle flips.
    void activate(u32 n)
    {
        const Entity entity = nodes[n].entity;
        if (world.has<UiButton>(entity))
        {
            push(UiEventType::Clicked, entity, 0.0f);
        }
        if (UiToggle* toggle = world.tryGet<UiToggle>(entity))
        {
            toggle->on = !toggle->on;
            world.markChanged<UiToggle>();
            push(UiEventType::ValueChanged, entity, toggle->on ? 1.0f : 0.0f);
        }
    }

    void pointer(const UiInput& input, u32 hit)
    {
        const bool pressedNow = input.pointerDown && !pointerWasDown;
        const bool releasedNow = !input.pointerDown && pointerWasDown;
        pointerWasDown = input.pointerDown;
        if (pressedNow && hit != kNoNode)
        {
            captured = nodes[hit].entity;
            capturedHint = hit;
            if (focusable(hit))
            {
                focusNode(hit, 0.0f);
            }
        }
        if (captured.isNull())
        {
            return;
        }
        if (input.pointerDown)
        {
            if (nodes[capturedHint].slider && input.pointerPresent)
            {
                dragSlider(capturedHint, input.pointerPixels);
            }
            return;
        }
        if (releasedNow && hit == capturedHint)
        {
            activate(capturedHint);
        }
        captured = {};
        capturedHint = kNoNode;
    }

    void focusFirst()
    {
        for (u32 c = static_cast<u32>(canvases.size()); c-- > firstEligible;)
        {
            for (u32 n = canvases[c].firstNode; n < canvases[c].endNode; ++n)
            {
                if (focusable(n))
                {
                    focusNode(n, 1.0f);
                    return;
                }
            }
        }
    }

    // The focusable node nearest to node @p from in @p direction, or kNoNode.
    [[nodiscard]] u32 nearestNode(u32 from, Direction direction) const
    {
        const Vec2 along = canvasDirection(direction);
        const Vec2 origin = centreOf(from);
        u32 best = kNoNode;
        f32 bestScore = 0.0f;
        for (u32 n = eligibleBegin(); n < nodes.size(); ++n)
        {
            if (n == from || !focusable(n))
            {
                continue;
            }
            const Vec2 offset = centreOf(n) - origin;
            const f32 advance = glm::dot(offset, along);
            if (advance < kMinAdvancePixels)
            {
                continue;
            }
            const f32 score = advance + kCrossPenalty * std::abs(offset.x * along.y - offset.y * along.x);
            if (best == kNoNode || score < bestScore)
            {
                best = n;
                bestScore = score;
            }
        }
        return best;
    }

    void moveFocus(Direction direction)
    {
        if (focus.isNull())
        {
            focusFirst();
            return;
        }
        const u32 n = focusHint;
        if (const UiSlider* slider = nodes[n].slider ? world.tryGet<UiSlider>(focus) : nullptr)
        {
            const bool vertical = slider->axis == UiAxis::Vertical;
            const f32 sign = vertical ? (direction == Direction::Down ? 1.0f
                                         : direction == Direction::Up ? -1.0f
                                                                      : 0.0f)
                                      : (direction == Direction::Right  ? 1.0f
                                         : direction == Direction::Left ? -1.0f
                                                                        : 0.0f);
            if (sign != 0.0f)
            {
                const f32 delta =
                    slider->step > 0.0f ? slider->step : slider->navigationStep * (slider->max - slider->min);
                setSliderValue(n, slider->value + sign * delta);
                return;
            }
        }
        if (const UiInteractable* interactable = world.tryGet<UiInteractable>(focus))
        {
            if (const u32 target = focusableNode(neighbourOf(*interactable, direction), kNoNode);
                target != kNoNode)
            {
                focusNode(target, 1.0f);
                return;
            }
        }
        if (const u32 nearest = nearestNode(n, direction); nearest != kNoNode)
        {
            focusNode(nearest, 1.0f);
        }
    }

    void navigate(const UiInput& input)
    {
        const Direction direction = directionOf(input.navigate);
        bool move = false;
        if (direction != Direction::None && direction != heldDirection)
        {
            move = true;
            repeatTimer = kRepeatDelay;
        }
        else if (direction != Direction::None)
        {
            repeatTimer -= std::isfinite(input.deltaSeconds) ? std::max(input.deltaSeconds, 0.0) : 0.0;
            if (repeatTimer <= 0.0)
            {
                move = true;
                repeatTimer = std::max(repeatTimer + kRepeatInterval, 0.0);
            }
        }
        heldDirection = direction;
        if (move)
        {
            moveFocus(direction);
        }
    }

    void submit(const UiInput& input)
    {
        const bool pressedNow = input.submitDown && !submitWasDown;
        const bool releasedNow = !input.submitDown && submitWasDown;
        submitWasDown = input.submitDown;
        if (pressedNow)
        {
            if (focus.isNull())
            {
                focusFirst();
            }
            else
            {
                submitted = focus;
            }
        }
        if (input.submitDown)
        {
            return;
        }
        if (releasedNow && !submitted.isNull() && submitted == focus)
        {
            activate(focusHint);
        }
        submitted = {};
    }

    [[nodiscard]] Entity cancelTarget() const noexcept
    {
        if (!focus.isNull())
        {
            return canvases[canvasOf(focusHint)].entity;
        }
        return modal ? canvases[firstEligible].entity : Entity{};
    }

    void writeInteractions()
    {
        bool written = false;
        for (const Node& node : nodes)
        {
            if (!node.interactable)
            {
                continue;
            }
            UiInteraction* state = world.tryGet<UiInteraction>(node.entity);
            const UiInteraction now = stateOf(node.entity);
            if (state != nullptr && !sameState(*state, now))
            {
                *state = now;
                written = true;
            }
        }
        if (written)
        {
            world.markChanged<UiInteraction>();
        }
    }

    // Hover, press, focus and navigation against the rectangles of this layout; values are written to the
    // Components and the parts of the layout that depend on them are updated.
    void interact(const UiInput& input)
    {
        ILL_PROFILE_SCOPE_N("UiWorld::interact");
        events.clear();
        droppedEvents = 0;
        focusHint = focusableNode(focus, focusHint);
        if (focusHint == kNoNode)
        {
            focus = {};
        }
        capturedHint = findNode(captured, capturedHint);
        if (capturedHint == kNoNode || enabledInteractable(capturedHint) == nullptr || hidden(capturedHint))
        {
            captured = {};
            capturedHint = kNoNode;
        }
        const u32 hit = input.pointerPresent ? hitTest(input.pointerPixels) : kNoNode;
        hovered = hit != kNoNode ? nodes[hit].entity : Entity{};
        pointer(input, hit);
        navigate(input);
        submit(input);
        if (input.cancelPressed)
        {
            push(UiEventType::Cancelled, cancelTarget(), 0.0f);
        }
        writeInteractions();
    }

    void setFocus(Entity entity)
    {
        if (entity.isNull())
        {
            focus = {};
            focusHint = kNoNode;
            return;
        }
        if (const u32 n = focusableNode(entity, kNoNode); n != kNoNode)
        {
            focus = entity;
            focusHint = n;
        }
    }

    void record(const UiFrame& frame)
    {
        ILL_PROFILE_SCOPE_N("UiWorld::record");
        const UiCanvasMetrics base = canvases.empty()
                                         ? canvasMetrics({}, frame.targetPixels, frame.safeInsetPixels)
                                         : canvases.front().metrics;
        drawList.begin(base);
        for (const CanvasEntry& canvas : canvases)
        {
            // Every canvas is drawn in the units of the first one.
            const f32 factor = canvas.metrics.pixelsPerUnit / base.pixelsPerUnit;
            clipStack.clear();
            u32 hiddenEnd = 0;
            for (u32 n = canvas.firstNode; n < canvas.endNode; ++n)
            {
                while (!clipStack.empty() && clipStack.back() <= n)
                {
                    drawList.popClip();
                    clipStack.pop_back();
                }
                const Node& node = nodes[n];
                if (n < hiddenEnd || hiddenByToggle(node))
                {
                    hiddenEnd = std::max(hiddenEnd, node.subtreeEnd);
                    continue;
                }
                drawList.setTransform(factor == 1.0f ? node.transform : scaled(node.transform, factor));
                drawNode(node);
                if (node.clip && node.subtreeEnd > n + 1 && drawList.pushClip(clipOf(node, factor)))
                {
                    clipStack.push_back(node.subtreeEnd);
                }
            }
            for (; !clipStack.empty(); clipStack.pop_back())
            {
                drawList.popClip();
            }
        }
        drawList.setTransform({});
        statistics.canvases = static_cast<u32>(canvases.size());
        statistics.elements = elements;
        statistics.skippedElements = skipped;
        statistics.missingKeys = missingKeys;
        statistics.droppedPrimitives = drawList.droppedPrimitives();
        statistics.missingGlyphs = drawList.missingGlyphs();
    }

    World& world;
    UiWorldDesc desc;
    UiDrawList drawList;
    HierarchyIndex hierarchy;
    LinearAllocator textArena;
    UiAssets* assets = nullptr;
    const text::Localization* localization = nullptr;
    std::vector<Node> nodes;
    std::vector<CanvasEntry> canvases;
    std::vector<u32> depthStack; // node of each depth on the path to the current Entity
    std::vector<u32> clipStack;  // subtreeEnd of each node that pushed a clip
    std::vector<HitClip> hitClips;
    std::vector<UiEvent> events;
    u32 droppedEvents = 0;
    u32 firstEligible = 0; // canvas index: the topmost modal canvas, or 0
    bool modal = false;
    Entity focus{};
    Entity hovered{};
    Entity captured{};  // pressed by the pointer until it is released
    Entity submitted{}; // pressed by submit until it is released
    u32 focusHint = kNoNode;
    u32 capturedHint = kNoNode;
    bool pointerWasDown = false;
    bool submitWasDown = false;
    Direction heldDirection = Direction::None;
    f64 repeatTimer = 0.0; // until the next repeat of the held direction
    Versions versions{};
    UiFrame lastFrame{};
    UiAssets* lastAssets = nullptr;
    const text::Localization* lastLocalization = nullptr;
    text::LocaleId lastLocale{};
    bool laidOut = false;
    bool pendingContent = false; // a measured text or image was still loading
    u32 elements = 0;
    u32 skipped = 0;
    u32 missingKeys = 0;
    UiWorldStatistics statistics{};
};

UiWorld::UiWorld(World& world, const UiWorldDesc& desc)
    : m_impl(std::make_unique<Impl>(world, desc))
{
}

UiWorld::~UiWorld() = default;

void UiWorld::setAssets(UiAssets* assets) noexcept
{
    m_impl->assets = assets;
}

void UiWorld::setLocalization(const text::Localization* localization) noexcept
{
    m_impl->localization = localization;
}

void UiWorld::update(const UiFrame& frame)
{
    ILL_PROFILE_SCOPE();
    m_impl->update(frame, UiInput{});
}

void UiWorld::update(const UiFrame& frame, const UiInput& input)
{
    ILL_PROFILE_SCOPE();
    m_impl->update(frame, input);
}

std::span<const UiEvent> UiWorld::events() const noexcept
{
    return m_impl->events;
}

Entity UiWorld::focused() const noexcept
{
    return m_impl->focus;
}

void UiWorld::setFocus(Entity entity)
{
    m_impl->setFocus(entity);
}

bool UiWorld::modalActive() const noexcept
{
    return m_impl->modal;
}

UiDrawData UiWorld::drawData() const noexcept
{
    return m_impl->drawList.data();
}

const UiWorldStatistics& UiWorld::statistics() const noexcept
{
    return m_impl->statistics;
}

} // namespace illusion
