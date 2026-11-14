#include <illusion/assets/Font.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <algorithm>
#include <array>
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
};

struct CanvasEntry
{
    Entity entity{};
    i32 order = 0;
    u32 firstNode = 0;
    u32 endNode = 0;
    UiCanvasMetrics metrics{};
};

// What a UiStack child measures: its text or image size, when it has either.
struct Content
{
    bool known = false;
    Vec2 size{0.0f};
};

// The Component versions a layout depends on.
constexpr usize kVersionCount = 8;
using Versions = std::array<u64, kVersionCount>;

Versions versionsOf(const World& world) noexcept
{
    return {world.componentVersion<UiCanvas>(),  world.componentVersion<UiElement>(),
            world.componentVersion<UiImage>(),   world.componentVersion<UiText>(),
            world.componentVersion<UiStack>(),   world.componentVersion<UiGrid>(),
            world.componentVersion<Hierarchy>(), world.componentVersion<Inactive>()};
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

    void update(const UiFrame& frame)
    {
        const bool rebuilt = hierarchy.update(world);
        const Versions now = versionsOf(world);
        statistics.layoutReused = !rebuilt && !changed(frame, now);
        if (!statistics.layoutReused)
        {
            layout(frame);
            versions = versionsOf(world);
            lastFrame = frame;
            lastAssets = assets;
            lastLocalization = localization;
            lastLocale = localization != nullptr ? localization->locale() : text::LocaleId{};
            laidOut = true;
        }
        record(frame);
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
        world.query<const UiCanvas>().without<Inactive>().each(
            [this](Entity entity, const UiCanvas& canvas) { canvases.push_back({entity, canvas.order}); });
        std::sort(canvases.begin(), canvases.end(), [](const CanvasEntry& a, const CanvasEntry& b) {
            return a.order != b.order ? a.order < b.order : a.entity.index < b.entity.index;
        });
        for (CanvasEntry& canvas : canvases)
        {
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
        }
        writeComputedRects();
    }

    u32 addNode(Entity entity, u32 parent)
    {
        const u32 id = static_cast<u32>(nodes.size());
        Node& node = nodes.emplace_back();
        node.entity = entity;
        node.parent = parent;
        node.element = world.has<UiElement>(entity);
        if (parent != kNoNode)
        {
            Node& parentNode = nodes[parent];
            if (parentNode.lastChild == kNoNode)
            {
                parentNode.firstChild = id;
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

    void writeComputedRects()
    {
        bool written = false;
        for (const Node& node : nodes)
        {
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
            for (u32 n = canvas.firstNode; n < canvas.endNode; ++n)
            {
                while (!clipStack.empty() && clipStack.back() <= n)
                {
                    drawList.popClip();
                    clipStack.pop_back();
                }
                const Node& node = nodes[n];
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
    m_impl->update(frame);
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
