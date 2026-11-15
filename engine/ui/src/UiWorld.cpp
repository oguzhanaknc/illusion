#include <illusion/assets/Font.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/text/Utf8.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
// Text fields: most code points, width of the caret in canvas units, half-period of its blink in seconds.
constexpr u32 kMaxFieldLength = 1024;
constexpr usize kMaxFieldBytes = usize{kMaxFieldLength} * 4;
constexpr f32 kCaretWidth = 2.0f;
constexpr f64 kBlinkSeconds = 0.5;
constexpr f32 kPlaceholderAlpha = 0.5f;
// A press inside a scroll view that moves farther than this, in pixels, drags the content.
constexpr f32 kDragThresholdPixels = 8.0f;
constexpr std::string_view kBullet = "\xE2\x80\xA2"; // U+2022
constexpr char32_t kBulletCodePoint = 0x2022;

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
    bool field = false;        // has UiTextField and UiText
    bool scroll = false;       // has UiScrollView
    Vec2 scrollMax{0.0f};      // largest offset of a scroll view on each axis
    UiRect bounds{};           // the node and its descendants after their transforms, canvas units
};

// A clip the recording pushed: subtreeEnd of its node, and whether it is a scroll view.
struct RecordClip
{
    u32 end = 0;
    bool scroll = false;
};

// What a UiTextField shows: its text, bullets for a password, or its placeholder; @p caret in its bytes.
struct FieldText
{
    std::string_view shown{};
    usize caret = 0;
    bool placeholder = false;
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

// Integral numbers up to this magnitude are formatted as integers (exact in an f64).
constexpr f64 kMaxExactInteger = 9007199254740992.0;

// The Component versions a layout depends on.
constexpr usize kVersionCount = 15;
using Versions = std::array<u64, kVersionCount>;

Versions versionsOf(const World& world) noexcept
{
    return {world.componentVersion<UiCanvas>(),       world.componentVersion<UiElement>(),
            world.componentVersion<UiImage>(),        world.componentVersion<UiText>(),
            world.componentVersion<UiStack>(),        world.componentVersion<UiGrid>(),
            world.componentVersion<Hierarchy>(),      world.componentVersion<Inactive>(),
            world.componentVersion<UiInteractable>(), world.componentVersion<UiToggle>(),
            world.componentVersion<UiSlider>(),       world.componentVersion<UiTextArguments>(),
            world.componentVersion<UiTextField>(),    world.componentVersion<UiScrollView>(),
            world.componentVersion<UiGlyph>()};
}

template <usize... I>
std::array<text::MessageArgument, sizeof...(I)> blankArguments(std::index_sequence<I...>) noexcept
{
    return {((void)I, text::MessageArgument(std::string_view(), i64{0}))...};
}

// The message arguments of @p stored; integral numbers become integers so that they print without decimals.
std::array<text::MessageArgument, UiTextArguments::kMaxArguments>
messageArguments(const UiTextArguments& stored) noexcept
{
    std::array<text::MessageArgument, UiTextArguments::kMaxArguments> arguments =
        blankArguments(std::make_index_sequence<UiTextArguments::kMaxArguments>());
    const u32 count = std::min(stored.count, UiTextArguments::kMaxArguments);
    for (u32 i = 0; i < count; ++i)
    {
        const UiTextArgumentValue& value = stored.arguments[i];
        const std::string_view name(value.name.data(), std::min<usize>(value.nameLength, value.name.size()));
        if (!value.isNumber)
        {
            arguments[i] = text::MessageArgument(
                name,
                std::string_view(value.text.data(), std::min<usize>(value.textLength, value.text.size())));
        }
        else if (std::abs(value.value) <= kMaxExactInteger && std::trunc(value.value) == value.value)
        {
            arguments[i] = text::MessageArgument(name, static_cast<i64>(value.value));
        }
        else
        {
            arguments[i] = text::MessageArgument(name, value.value);
        }
    }
    return arguments;
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

bool overlaps(const UiRect& a, const UiRect& b) noexcept
{
    return a.max.x > b.min.x && a.max.y > b.min.y && a.min.x < b.max.x && a.min.y < b.max.y;
}

UiRect transformedBounds(const UiTransform2D& transform, const UiRect& rect) noexcept
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

// Characters a text field accepts: no control characters, surrogates or values beyond Unicode.
bool typable(char32_t c) noexcept
{
    return c >= 0x20 && !(c >= 0x7F && c <= 0x9F) && !(c >= 0xD800 && c <= 0xDFFF) && c <= 0x10FFFF;
}

// The byte offset of the code point before @p offset, which is a code point boundary greater than 0.
usize previousBoundary(std::string_view utf8, usize offset) noexcept
{
    usize at = 0;
    while (true)
    {
        const usize next = at + text::decodeUtf8(utf8, at).length;
        if (next >= offset)
        {
            return at;
        }
        at = next;
    }
}

// The byte offset after the first @p count code points of @p utf8.
usize codePointOffset(std::string_view utf8, usize count) noexcept
{
    usize at = 0;
    for (usize i = 0; i < count && at < utf8.size(); ++i)
    {
        at += text::decodeUtf8(utf8, at).length;
    }
    return at;
}

u32 fieldLength(const UiTextField& field) noexcept
{
    return std::clamp(field.maxLength, 1u, kMaxFieldLength);
}

// A text field draws one line from its left edge; it aligns and scrolls the line itself.
UiTextStyle fieldStyle(const UiText& text) noexcept
{
    UiTextStyle style = textStyle(text, 0.0f);
    style.align = UiTextAlign::Start;
    style.wrapWidth = 0.0f;
    return style;
}

f32 lineHeightOf(const Font& font, const UiTextStyle& style) noexcept
{
    return font.metrics().lineHeight() * style.size * style.lineSpacing;
}

f32 finiteOr(f32 value, f32 fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace

f32 snapSliderValue(const UiSlider& slider, f32 value) noexcept
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
        fieldShadow.reserve(kMaxFieldBytes);
        shownScratch.reserve(kMaxFieldBytes);
    }

    [[nodiscard]] std::string_view textOf(const Node& node, const UiText& text) const noexcept
    {
        return node.keyed ? node.keyText : std::string_view(text.text);
    }

    [[nodiscard]] bool changed(const UiFrame& frame, const Versions& now) const noexcept
    {
        if (!laidOut || pendingContent || textEdited || now != versions ||
            frame.targetPixels != lastFrame.targetPixels ||
            !(frame.safeInsetPixels == lastFrame.safeInsetPixels) || assets != lastAssets ||
            localization != lastLocalization || glyphs != lastGlyphs ||
            (glyphs != nullptr && glyphs->version() != lastGlyphVersion))
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
            lastGlyphs = glyphs;
            lastGlyphVersion = glyphs != nullptr ? glyphs->version() : 0;
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
        textEdited = false;
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
        boundsDirty = true;
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
            node.field = world.has<UiTextField>(entity) && world.has<UiText>(entity);
            node.scroll = world.has<UiScrollView>(entity);
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
        if (text == nullptr)
        {
            return;
        }
        if (const UiGlyph* glyph = glyphs != nullptr ? world.tryGet<UiGlyph>(node.entity) : nullptr)
        {
            if (const std::string_view label = glyphs->actionLabel(glyph->action); !label.empty())
            {
                text::TextOutput out(textArena);
                out.append(label);
                node.keyed = true;
                node.keyText = out.view();
                return;
            }
        }
        if (text->key.empty())
        {
            return;
        }
        node.keyed = true;
        text::TextOutput out(textArena);
        const text::MessageFormat* message =
            localization != nullptr ? localization->find(text->key) : nullptr;
        if (message != nullptr)
        {
            const UiTextArguments* stored = world.tryGet<UiTextArguments>(node.entity);
            if (stored != nullptr && stored->count > 0)
            {
                const std::array<text::MessageArgument, UiTextArguments::kMaxArguments> arguments =
                    messageArguments(*stored);
                (void)message->format(
                    std::span(arguments).first(std::min(stored->count, UiTextArguments::kMaxArguments)),
                    localization->context(), out);
            }
            else
            {
                (void)message->format(std::span<const text::MessageArgument>(), localization->context(), out);
            }
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
            node.clip = element->clipChildren || node.scroll;
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
            if (node.field)
            {
                const FieldText shown = fieldText(world.get<UiTextField>(node.entity), *face.font, 0);
                return {true, measureText(*face.font, shown.shown, fieldStyle(*text)).size};
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
        if (node.scroll)
        {
            layoutScroll(n);
        }
        else if (const UiStack* stack = world.tryGet<UiStack>(node.entity))
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

    // Lays out the subtree of node @p n again after a value it depends on changed (a slider's value, a
    // scroll view's offset).
    void relayoutSubtree(u32 n)
    {
        layoutChildren(n);
        const u32 end = nodes[n].subtreeEnd;
        for (u32 m = n + 1; m < end; ++m)
        {
            finishNode(m);
            layoutChildren(m);
        }
        writeComputedRects(n + 1, end);
        boundsDirty = true;
    }

    // The first child is the content: at the view's top left with its own size, moved by the offset, which
    // is kept within the content; the other children are placed as usual.
    void layoutScroll(u32 n)
    {
        const UiRect view = nodes[n].rect;
        const u32 content = nodes[n].firstChild;
        for (u32 c = content != kNoNode ? nodes[content].nextSibling : kNoNode; c != kNoNode;
             c = nodes[c].nextSibling)
        {
            nodes[c].rect = resolvePlacement(world.get<UiElement>(nodes[c].entity).placement, view);
        }
        UiScrollView& scroll = world.get<UiScrollView>(nodes[n].entity);
        const Vec2 size = content != kNoNode ? contentSize(content, view.size()) : view.size();
        Vec2 range = glm::max(size - view.size(), Vec2(0.0f));
        range.x = scroll.horizontal ? range.x : 0.0f;
        range.y = scroll.vertical ? range.y : 0.0f;
        nodes[n].scrollMax = range;
        const Vec2 offset = glm::clamp(Vec2(finiteOr(scroll.offset.x, 0.0f), finiteOr(scroll.offset.y, 0.0f)),
                                       Vec2(0.0f), range);
        if (offset != scroll.offset && !desc.preview)
        {
            scroll.offset = offset;
            world.markChanged<UiScrollView>();
        }
        if (content != kNoNode)
        {
            const Vec2 min = view.min - offset;
            nodes[content].rect = {min, min + size};
        }
    }

    // Size of the content of a scroll view of @p viewSize: UiElement::size, else for a stack its length
    // along its axis, else the view's size.
    [[nodiscard]] Vec2 contentSize(u32 content, Vec2 viewSize)
    {
        const Vec2 fixed = glm::max(world.get<UiElement>(nodes[content].entity).size, Vec2(0.0f));
        Vec2 size(fixed.x > 0.0f ? fixed.x : viewSize.x, fixed.y > 0.0f ? fixed.y : viewSize.y);
        if (const UiStack* stack = world.tryGet<UiStack>(nodes[content].entity))
        {
            const int along = stack->axis == UiAxis::Vertical ? 1 : 0;
            if (fixed[along] <= 0.0f)
            {
                size[along] = stackLength(content, *stack, size);
            }
        }
        return size;
    }

    // What child @p c of a stack with inner size @p innerSize measures along and across it; kStretch along
    // when it shares the rest.
    [[nodiscard]] Vec2 stackChildSize(u32 c, const UiStack& stack, Vec2 innerSize)
    {
        const bool vertical = stack.axis == UiAxis::Vertical;
        const int along = vertical ? 1 : 0;
        const int across = 1 - along;
        const Vec2 fixed = glm::max(world.get<UiElement>(nodes[c].entity).size, Vec2(0.0f));
        const f32 wrapAt =
            vertical ? (stack.crossAlign != UiCrossAlign::Stretch && fixed.x > 0.0f ? fixed.x : innerSize.x)
                     : (fixed.x > 0.0f ? fixed.x : innerSize.x);
        const Content content = contentOf(nodes[c], wrapAt);
        Vec2 size(0.0f);
        size[along] = fixed[along] > 0.0f ? fixed[along] : content.known ? content.size[along] : kStretch;
        size[across] = stack.crossAlign == UiCrossAlign::Stretch ? innerSize[across]
                       : fixed[across] > 0.0f                    ? fixed[across]
                       : content.known                           ? content.size[across]
                                                                 : innerSize[across];
        return size;
    }

    // Length of stack node @p n of @p size along its axis: padding, children that do not stretch, gaps.
    [[nodiscard]] f32 stackLength(u32 n, const UiStack& stack, Vec2 size)
    {
        const int along = stack.axis == UiAxis::Vertical ? 1 : 0;
        const Vec2 inner = inset({Vec2(0.0f), size}, stack.padding).size();
        f32 length = size[along] - inner[along];
        for (u32 c = nodes[n].firstChild; c != kNoNode; c = nodes[c].nextSibling)
        {
            length += std::max(stackChildSize(c, stack, inner)[along], 0.0f);
        }
        if (nodes[n].childCount > 1)
        {
            length += std::max(stack.spacing, 0.0f) * static_cast<f32>(nodes[n].childCount - 1);
        }
        return length;
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
            const Vec2 size = stackChildSize(c, stack, innerSize);
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

    // The bounds of nodes [0, size): each node's rectangle after its transform, joined with its descendants'.
    void computeBounds()
    {
        for (Node& node : nodes)
        {
            node.bounds =
                axisAligned(node.transform) && node.transform.x.x > 0.0f && node.transform.y.y > 0.0f
                    ? UiRect{node.transform.apply(node.rect.min), node.transform.apply(node.rect.max)}
                    : transformedBounds(node.transform, node.rect);
        }
        for (u32 n = static_cast<u32>(nodes.size()); n-- > 0;)
        {
            if (const u32 parent = nodes[n].parent; parent != kNoNode)
            {
                nodes[parent].bounds = {glm::min(nodes[parent].bounds.min, nodes[n].bounds.min),
                                        glm::max(nodes[parent].bounds.max, nodes[n].bounds.max)};
            }
        }
        boundsDirty = false;
    }

    // What text field @p field shows with its caret at byte @p caretAt; a password's bullets are written to
    // shownScratch, which the next call overwrites.
    [[nodiscard]] FieldText fieldText(const UiTextField& field, const Font& font, usize caretAt)
    {
        if (field.text.empty())
        {
            return {field.placeholder, 0, true};
        }
        if (!field.password)
        {
            return {field.text, std::min(caretAt, field.text.size()), false};
        }
        const std::string_view bullet =
            font.find(kBulletCodePoint) != nullptr ? kBullet : std::string_view("*");
        shownScratch.clear();
        usize caret = 0;
        for (usize at = 0; at < field.text.size(); at += text::decodeUtf8(field.text, at).length)
        {
            if (at == caretAt)
            {
                caret = shownScratch.size();
            }
            shownScratch.append(bullet);
        }
        if (caretAt >= field.text.size())
        {
            caret = shownScratch.size();
        }
        return {shownScratch, caret, false};
    }

    // Left edge of the line of field node @p node that is @p width wide: aligned in the element when it fits,
    // else scrolled by @p scroll from the element's left edge.
    [[nodiscard]] static f32 fieldOrigin(const Node& node, const UiText& text, f32 width, f32 scroll) noexcept
    {
        const f32 room = node.rect.size().x;
        if (width > room)
        {
            return node.rect.min.x - scroll;
        }
        switch (text.align)
        {
        case UiTextAlign::Center:
            return node.rect.min.x + (room - width) * 0.5f;
        case UiTextAlign::End:
            return node.rect.max.x - width;
        case UiTextAlign::Start:
        default:
            return node.rect.min.x;
        }
    }

    // The scroll of the focused field: unchanged while the caret is visible, else just enough to show it.
    void updateFieldScroll()
    {
        if (caretField.isNull() || assets == nullptr)
        {
            return;
        }
        const Node& node = nodes[focusHint];
        const UiText& text = world.get<UiText>(node.entity);
        const UiFontFace face = assets->font(text.font);
        if (face.font == nullptr)
        {
            return;
        }
        const FieldText shown = fieldText(world.get<UiTextField>(node.entity), *face.font, caretByte);
        const UiTextStyle style = fieldStyle(text);
        const f32 width = measureText(*face.font, shown.shown, style).size.x;
        const f32 room = node.rect.size().x;
        if (width <= room)
        {
            fieldScroll = 0.0f;
            return;
        }
        const f32 caretX = measureText(*face.font, shown.shown.substr(0, shown.caret), style).size.x;
        const f32 visible = std::max(room - kCaretWidth, 0.0f);
        if (caretX - fieldScroll > visible)
        {
            fieldScroll = caretX - visible;
        }
        if (caretX < fieldScroll)
        {
            fieldScroll = caretX;
        }
        fieldScroll = std::clamp(fieldScroll, 0.0f, width - room + kCaretWidth);
    }

    [[nodiscard]] bool caretVisible() const noexcept
    {
        return std::fmod(blinkSeconds, 2.0 * kBlinkSeconds) < kBlinkSeconds;
    }

    // A text field: its line clipped to the element, aligned or scrolled, and the caret while it is focused.
    void drawField(const Node& node, const UiText& text, f32 factor)
    {
        const UiFontFace face = assets->font(text.font);
        if (face.font == nullptr)
        {
            return;
        }
        const bool focused = node.entity == caretField;
        const FieldText shown =
            fieldText(world.get<UiTextField>(node.entity), *face.font, focused ? caretByte : 0);
        UiTextStyle style = fieldStyle(text);
        style.color.a *= node.opacity * (shown.placeholder ? kPlaceholderAlpha : 1.0f);
        const f32 width = measureText(*face.font, shown.shown, style).size.x;
        const f32 lineHeight = lineHeightOf(*face.font, style);
        Vec2 position(fieldOrigin(node, text, width, focused ? fieldScroll : 0.0f), node.rect.min.y);
        if (text.verticalAlign == UiVerticalAlign::Middle)
        {
            position.y += (node.rect.size().y - lineHeight) * 0.5f;
        }
        else if (text.verticalAlign == UiVerticalAlign::Bottom)
        {
            position.y = node.rect.max.y - lineHeight;
        }
        const bool clipped = drawList.pushClip(clipOf(node, factor));
        (void)drawList.text(face, position, shown.shown, style);
        if (focused && caretVisible())
        {
            const f32 x =
                position.x + measureText(*face.font, shown.shown.substr(0, shown.caret), style).size.x;
            Vec4 color = world.get<UiTextField>(node.entity).caretColor;
            color.a *= node.opacity;
            (void)drawList.rect({{x, position.y}, {x + kCaretWidth, position.y + lineHeight}}, color);
        }
        if (clipped)
        {
            drawList.popClip();
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

    void drawNode(const Node& node, f32 factor)
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
        if (node.field)
        {
            drawField(node, *text, factor);
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
        if (byNavigation > 0.0f)
        {
            scrollIntoView(n);
        }
    }

    // The topmost enabled interactable node under @p pixels, or kNoNode.
    [[nodiscard]] u32 hitTest(Vec2 pixels)
    {
        return topmostAt(pixels, [this](u32 n) { return enabledInteractable(n) != nullptr; });
    }

    // The topmost scroll view under @p pixels, or kNoNode.
    [[nodiscard]] u32 scrollViewAt(Vec2 pixels)
    {
        return topmostAt(pixels, [this](u32 n) { return nodes[n].scroll; });
    }

    // The topmost node under @p pixels for which @p accepts holds, inside the clips of its ancestors.
    template <class Accepts>
    [[nodiscard]] u32 topmostAt(Vec2 pixels, const Accepts& accepts)
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
                if (outside == 0 && node.opacity > 0.0f && accepts(n) &&
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
        const f32 next = snapSliderValue(*slider, value);
        if (next == slider->value)
        {
            return;
        }
        slider->value = next;
        world.markChanged<UiSlider>();
        push(UiEventType::ValueChanged, entity, next);
        relayoutSubtree(n);
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

    // Moves scroll view node @p n by @p delta canvas units within its range. Its content only moves, so its
    // subtree is translated where a new layout would place it instead of being laid out again.
    void scrollBy(u32 n, Vec2 delta)
    {
        UiScrollView* view = world.tryGet<UiScrollView>(nodes[n].entity);
        const u32 content = nodes[n].firstChild;
        if (view == nullptr || content == kNoNode || !std::isfinite(delta.x) || !std::isfinite(delta.y))
        {
            return;
        }
        const Vec2 next = glm::clamp(view->offset + delta, Vec2(0.0f), nodes[n].scrollMax);
        if (next == view->offset)
        {
            return;
        }
        const Vec2 shift = view->offset - next;
        view->offset = next;
        world.markChanged<UiScrollView>();
        // A descendant's transform T becomes p -> T(p - shift) + shift in the view's space, whose linear part
        // is the view's.
        const UiTransform2D& outer = nodes[n].transform;
        const Vec2 moved = outer.x * shift.x + outer.y * shift.y;
        const u32 end = nodes[content].subtreeEnd;
        for (u32 m = content; m < end; ++m)
        {
            Node& node = nodes[m];
            node.rect = {node.rect.min + shift, node.rect.max + shift};
            node.transform.translation += moved - (node.transform.x * shift.x + node.transform.y * shift.y);
        }
        writeComputedRects(content, end);
        boundsDirty = true;
    }

    // Scrolls the views around node @p n, innermost first, until it is inside each of them.
    void scrollIntoView(u32 n)
    {
        for (u32 view = nodes[n].parent; view != kNoNode; view = nodes[view].parent)
        {
            if (!nodes[view].scroll)
            {
                continue;
            }
            const UiRect& shown = nodes[view].rect;
            const UiRect& target = nodes[n].rect;
            Vec2 delta(0.0f);
            for (glm::length_t axis = 0; axis < 2; ++axis)
            {
                if (target.min[axis] < shown.min[axis])
                {
                    delta[axis] = target.min[axis] - shown.min[axis];
                }
                else if (target.max[axis] > shown.max[axis])
                {
                    delta[axis] =
                        std::min(target.max[axis] - shown.max[axis], target.min[axis] - shown.min[axis]);
                }
            }
            scrollBy(view, delta);
        }
    }

    // The wheel scrolls the topmost view under the pointer, vertically when it may.
    void wheel(const UiInput& input)
    {
        if (!input.pointerPresent || !std::isfinite(input.wheel) || input.wheel == 0.0f)
        {
            return;
        }
        const u32 view = scrollViewAt(input.pointerPixels);
        if (view == kNoNode)
        {
            return;
        }
        const UiScrollView& settings = world.get<UiScrollView>(nodes[view].entity);
        const f32 distance = -input.wheel * finiteOr(settings.wheelStep, 0.0f);
        if (settings.vertical)
        {
            scrollBy(view, {0.0f, distance});
        }
        else if (settings.horizontal)
        {
            scrollBy(view, {distance, 0.0f});
        }
    }

    [[nodiscard]] bool inSubtree(u32 root, u32 n) const noexcept
    {
        return n > root && n < nodes[root].subtreeEnd;
    }

    // A press on a scroll view outside any interactable element drags its content at once; a press on one
    // inside it once the pointer moved beyond the threshold, which takes the press from that element.
    void drag(const UiInput& input, u32 hit, bool pressedNow)
    {
        if (pressedNow && input.pointerPresent)
        {
            const u32 view = scrollViewAt(input.pointerPixels);
            const bool overWidget = hit != kNoNode && hit != view;
            if (view != kNoNode && (!overWidget || (inSubtree(view, hit) && !nodes[hit].slider)))
            {
                dragView = nodes[view].entity;
                dragHint = view;
                dragStart = input.pointerPixels;
                dragOffset = world.get<UiScrollView>(dragView).offset;
                dragging = !overWidget;
            }
        }
        if (dragView.isNull())
        {
            return;
        }
        dragHint = findNode(dragView, dragHint);
        if (!input.pointerDown || dragHint == kNoNode)
        {
            dragView = {};
            dragHint = kNoNode;
            dragging = false;
            return;
        }
        if (!input.pointerPresent)
        {
            return;
        }
        const Vec2 moved = input.pointerPixels - dragStart;
        if (!dragging && glm::dot(moved, moved) > kDragThresholdPixels * kDragThresholdPixels)
        {
            dragging = true;
            captured = {};
            capturedHint = kNoNode;
        }
        if (dragging)
        {
            const UiScrollView& view = world.get<UiScrollView>(dragView);
            const Vec2 target = dragOffset - moved / canvases[canvasOf(dragHint)].metrics.pixelsPerUnit;
            scrollBy(dragHint, target - view.offset);
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
                if (nodes[hit].field && syncField() != nullptr)
                {
                    placeCaret(hit, input.pointerPixels);
                }
            }
        }
        drag(input, hit, pressedNow);
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
        if (nodes[n].field && (direction == Direction::Left || direction == Direction::Right))
        {
            if (UiTextField* field = syncField())
            {
                moveCaret(*field, direction == Direction::Right);
            }
            return;
        }
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
            else if (nodes[focusHint].field)
            {
                push(UiEventType::Submitted, focus, 0.0f);
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

    // The UiTextField of the focused element, with the caret state made to match it: at the end when the
    // field got the focus or its text was set from outside. Null when no field has the focus.
    [[nodiscard]] UiTextField* syncField()
    {
        UiTextField* field = !focus.isNull() && focusHint != kNoNode && nodes[focusHint].field
                                 ? world.tryGet<UiTextField>(focus)
                                 : nullptr;
        if (field == nullptr)
        {
            caretField = {};
            return nullptr;
        }
        if (caretField != focus)
        {
            caretField = focus;
            field->text.reserve(usize{fieldLength(*field)} * 4);
            fieldScroll = 0.0f;
        }
        else if (fieldShadow == field->text)
        {
            return field;
        }
        fieldShadow = field->text;
        caretByte = field->text.size();
        caretTouched = true;
        return field;
    }

    void moveCaret(const UiTextField& field, bool forward)
    {
        if (forward && caretByte < field.text.size())
        {
            caretByte += text::decodeUtf8(field.text, caretByte).length;
        }
        else if (!forward && caretByte > 0)
        {
            caretByte = previousBoundary(field.text, caretByte);
        }
        caretTouched = true;
    }

    // Puts the caret of field node @p n at the glyph boundary nearest to @p pixels, measured as it is drawn.
    void placeCaret(u32 n, Vec2 pixels)
    {
        const Node& node = nodes[n];
        const UiText& text = world.get<UiText>(node.entity);
        const UiFontFace face = assets != nullptr ? assets->font(text.font) : UiFontFace{};
        Vec2 local(0.0f);
        if (face.font == nullptr ||
            !untransform(node.transform, pixels / canvases[canvasOf(n)].metrics.pixelsPerUnit, local))
        {
            return;
        }
        const UiTextField& field = world.get<UiTextField>(node.entity);
        const FieldText shown = fieldText(field, *face.font, caretByte);
        if (shown.placeholder)
        {
            caretByte = 0;
            return;
        }
        const UiTextStyle style = fieldStyle(text);
        const f32 width = measureText(*face.font, shown.shown, style).size.x;
        const f32 x = local.x - fieldOrigin(node, text, width, fieldScroll);
        const auto widthBefore = [&](usize count) {
            return measureText(*face.font, shown.shown.substr(0, codePointOffset(shown.shown, count)), style)
                .size.x;
        };
        // Prefix widths grow with the count: the first boundary at or right of x, or the one before it.
        usize low = 0;
        usize high = text::utf8Length(shown.shown);
        while (low < high)
        {
            const usize middle = low + (high - low) / 2;
            if (widthBefore(middle) < x)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low > 0 && x - widthBefore(low - 1) < widthBefore(low) - x)
        {
            --low;
        }
        caretByte = codePointOffset(field.text, low);
        caretTouched = true;
    }

    // Typed characters, Backspace, Delete and the caret keys on the focused text field.
    void editField(const UiInput& input)
    {
        UiTextField* field = syncField();
        if (field == nullptr)
        {
            return;
        }
        bool edited = false;
        std::string& value = field->text;
        usize length = text::utf8Length(value);
        for (const char32_t c : input.text)
        {
            char bytes[4];
            const u32 size = typable(c) && length < fieldLength(*field) ? text::encodeUtf8(c, bytes) : 0;
            if (size == 0)
            {
                continue;
            }
            value.insert(caretByte, bytes, size);
            caretByte += size;
            ++length;
            edited = true;
        }
        if (input.backspace && caretByte > 0)
        {
            const usize start = previousBoundary(value, caretByte);
            value.erase(start, caretByte - start);
            caretByte = start;
            edited = true;
        }
        if (input.deleteForward && caretByte < value.size())
        {
            value.erase(caretByte, text::decodeUtf8(value, caretByte).length);
            edited = true;
        }
        if (input.caretLeft != input.caretRight)
        {
            moveCaret(*field, input.caretRight);
        }
        if (input.caretHome)
        {
            caretByte = 0;
            caretTouched = true;
        }
        if (input.caretEnd)
        {
            caretByte = value.size();
            caretTouched = true;
        }
        if (edited)
        {
            fieldShadow = value;
            caretTouched = true;
            textEdited = true;
            world.markChanged<UiTextField>();
            push(UiEventType::TextChanged, focus, 0.0f);
        }
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
        caretTouched = false;
        (void)syncField();
        const u32 hit = input.pointerPresent ? hitTest(input.pointerPixels) : kNoNode;
        hovered = hit != kNoNode ? nodes[hit].entity : Entity{};
        pointer(input, hit);
        wheel(input);
        navigate(input);
        submit(input);
        editField(input);
        if (input.cancelPressed)
        {
            push(UiEventType::Cancelled, cancelTarget(), 0.0f);
        }
        writeInteractions();
        updateFieldScroll();
        const f64 elapsed = std::isfinite(input.deltaSeconds) ? std::max(input.deltaSeconds, 0.0) : 0.0;
        blinkSeconds = caretTouched ? 0.0 : blinkSeconds + elapsed;
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
        if (boundsDirty)
        {
            computeBounds();
        }
        for (const CanvasEntry& canvas : canvases)
        {
            // Every canvas is drawn in the units of the first one.
            const f32 factor = canvas.metrics.pixelsPerUnit / base.pixelsPerUnit;
            clipStack.clear();
            u32 scrollClips = 0;
            u32 hiddenEnd = 0;
            for (u32 n = canvas.firstNode; n < canvas.endNode; ++n)
            {
                while (!clipStack.empty() && clipStack.back().end <= n)
                {
                    drawList.popClip();
                    scrollClips -= clipStack.back().scroll ? 1u : 0u;
                    clipStack.pop_back();
                }
                const Node& node = nodes[n];
                if (n < hiddenEnd || hiddenByToggle(node))
                {
                    hiddenEnd = std::max(hiddenEnd, node.subtreeEnd);
                    continue;
                }
                // Inside a scroll view, subtrees wholly outside the clip are skipped.
                if (scrollClips > 0 &&
                    !overlaps({node.bounds.min * factor, node.bounds.max * factor}, drawList.clip()))
                {
                    n = node.subtreeEnd - 1;
                    continue;
                }
                drawList.setTransform(factor == 1.0f ? node.transform : scaled(node.transform, factor));
                drawNode(node, factor);
                if (node.clip && node.subtreeEnd > n + 1 && drawList.pushClip(clipOf(node, factor)))
                {
                    clipStack.push_back({node.subtreeEnd, node.scroll});
                    scrollClips += node.scroll ? 1u : 0u;
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
    UiGlyphSource* glyphs = nullptr;
    UiGlyphSource* lastGlyphs = nullptr;
    u64 lastGlyphVersion = 0;
    std::vector<Node> nodes;
    std::vector<CanvasEntry> canvases;
    std::vector<u32> depthStack; // node of each depth on the path to the current Entity
    std::vector<RecordClip> clipStack;
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
    bool textEdited = false;     // a text field changed: its measured size may have too
    bool boundsDirty = true;
    // The caret of the focused text field.
    Entity caretField{};
    usize caretByte = 0;
    f32 fieldScroll = 0.0f; // canvas units the field's line is scrolled left
    f64 blinkSeconds = 0.0; // since the caret last moved
    bool caretTouched = false;
    std::string fieldShadow; // the text the caret was placed in, to notice texts set from outside
    std::string shownScratch;
    // A drag of a scroll view's content.
    Entity dragView{};
    u32 dragHint = kNoNode;
    Vec2 dragStart{0.0f};
    Vec2 dragOffset{0.0f};
    bool dragging = false;
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

void UiWorld::setGlyphs(UiGlyphSource* glyphs) noexcept
{
    m_impl->glyphs = glyphs;
}

void UiWorld::invalidateLayout() noexcept
{
    m_impl->laidOut = false;
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

void UiWorld::layout(UiLayout& out) const
{
    const Impl& impl = *m_impl;
    out.canvases.clear();
    out.elements.clear();
    std::vector<u32> elementOf(impl.nodes.size(), UiLayoutElement::kNoParent);
    for (u32 c = 0; c < impl.canvases.size(); ++c)
    {
        const CanvasEntry& canvas = impl.canvases[c];
        out.canvases.push_back(
            {.entity = canvas.entity,
             .order = canvas.order,
             .metrics = canvas.metrics,
             .area = canvas.endNode > canvas.firstNode ? impl.nodes[canvas.firstNode].rect : UiRect{},
             .modal = canvas.modal,
             .pointerEligible = c >= impl.firstEligible});
        for (u32 n = canvas.firstNode + 1; n < canvas.endNode; ++n)
        {
            const Node& node = impl.nodes[n];
            // Entities destroyed since the update are left out.
            if (!impl.world.isAlive(node.entity))
            {
                continue;
            }
            elementOf[n] = static_cast<u32>(out.elements.size());
            out.elements.push_back(
                {.entity = node.entity,
                 .canvas = c,
                 .parent = node.parent != kNoNode ? elementOf[node.parent] : UiLayoutElement::kNoParent,
                 .rect = node.rect,
                 .transform = node.transform,
                 .opacity = node.opacity,
                 .visible = node.opacity > 0.0f && !impl.hidden(n),
                 .clipsChildren = node.clip,
                 .interactable = node.interactable});
        }
    }
}

Entity UiWorld::hitTest(Vec2 pixels)
{
    Impl& impl = *m_impl;
    // Unlike the update's own hit test, the World may have changed since the layout.
    const u32 hit = impl.topmostAt(pixels, [&impl](u32 n) {
        return impl.world.isAlive(impl.nodes[n].entity) && impl.enabledInteractable(n) != nullptr;
    });
    return hit != kNoNode ? impl.nodes[hit].entity : Entity{};
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
