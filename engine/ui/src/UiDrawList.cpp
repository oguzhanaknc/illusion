#include <illusion/assets/Font.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/text/Utf8.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiDrawList.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace illusion
{

// The saved Components are plain data that the ECS can move (their reflection comes with UI-2a).
static_assert(std::is_nothrow_move_constructible_v<UiCanvas> &&
              std::is_nothrow_move_constructible_v<UiElement> &&
              std::is_nothrow_move_constructible_v<UiImage> && std::is_nothrow_move_constructible_v<UiText> &&
              std::is_nothrow_move_constructible_v<UiStack> && std::is_nothrow_move_constructible_v<UiGrid> &&
              std::is_nothrow_move_constructible_v<UiComputedRect>);

namespace
{

bool finite(f32 value) noexcept
{
    return std::isfinite(value);
}
bool finite(Vec2 v) noexcept
{
    return finite(v.x) && finite(v.y);
}
bool finite(Vec4 v) noexcept
{
    return finite(v.x) && finite(v.y) && finite(v.z) && finite(v.w);
}
bool finite(const UiRect& rect) noexcept
{
    return finite(rect.min) && finite(rect.max);
}

// Correctly rounded 8-bit sRGB encoding: thresholds[k] is the linear value at which the encoding steps from
// k to k + 1 (the decoded midpoint between them); start[i] is the encoding of i / kBuckets, from which at
// most a step or two reach the answer.
struct SrgbEncoder
{
    static constexpr usize kBuckets = 4096;

    std::array<f32, 256> thresholds{};
    std::array<u8, kBuckets + 1> start{};

    SrgbEncoder() noexcept
    {
        for (usize k = 0; k < 255; ++k)
        {
            const f64 encoded = (static_cast<f64>(k) + 0.5) / 255.0;
            const f64 linear =
                encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
            thresholds[k] = static_cast<f32>(linear);
        }
        thresholds[255] = std::numeric_limits<f32>::infinity();
        for (usize i = 0; i <= kBuckets; ++i)
        {
            const f32 linear = static_cast<f32>(i) / static_cast<f32>(kBuckets);
            start[i] = static_cast<u8>(std::upper_bound(thresholds.begin(), thresholds.end(), linear) -
                                       thresholds.begin());
        }
    }

    // @p linear is in [0, 1].
    [[nodiscard]] u32 encode(f32 linear) const noexcept
    {
        u32 k = start[static_cast<usize>(linear * static_cast<f32>(kBuckets))];
        while (linear >= thresholds[k])
        {
            ++k;
        }
        return k;
    }
};

const SrgbEncoder& srgbEncoder() noexcept
{
    static const SrgbEncoder encoder;
    return encoder;
}

u32 packColor(Vec4 color) noexcept
{
    const Vec4 c = glm::clamp(color, Vec4(0.0f), Vec4(1.0f));
    const SrgbEncoder& srgb = srgbEncoder();
    const auto alpha = static_cast<u32>(static_cast<f64>(c.a) * 255.0 + 0.5); // f32 would round 0.7 * 255 up
    return srgb.encode(c.r) | (srgb.encode(c.g) << 8u) | (srgb.encode(c.b) << 16u) | (alpha << 24u);
}

struct Quad
{
    std::array<Vec2, 4> position{}; // top left, top right, bottom right, bottom left
    std::array<Vec2, 4> uv{};
};

Quad makeQuad(const UiRect& rect, const UiRect& uv) noexcept
{
    Quad quad;
    quad.position = {rect.min, Vec2(rect.max.x, rect.min.y), rect.max, Vec2(rect.min.x, rect.max.y)};
    quad.uv = {uv.min, Vec2(uv.max.x, uv.min.y), uv.max, Vec2(uv.min.x, uv.max.y)};
    return quad;
}

// Writes quads into a list's buffers, which are reserved: nothing here allocates.
struct QuadSink
{
    std::vector<UiVertex>& vertices;
    std::vector<u32>& indices;
    std::vector<UiDrawCommand>& commands;
    const UiDrawListDesc& desc;
    const UiTransform2D& transform;
    bool identity;
    const UiRect& clip;

    // Applies the transform; false when a corner is not finite.
    [[nodiscard]] bool place(Quad& quad) const noexcept
    {
        for (Vec2& p : quad.position)
        {
            if (!identity)
            {
                p = transform.apply(p);
            }
            if (!finite(p))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool visible(const Quad& quad) const noexcept
    {
        Vec2 min = quad.position[0];
        Vec2 max = quad.position[0];
        for (const Vec2 p : quad.position)
        {
            min = glm::min(min, p);
            max = glm::max(max, p);
        }
        return max.x > clip.min.x && max.y > clip.min.y && min.x < clip.max.x && min.y < clip.max.y;
    }

    // Makes room for @p quads in a command with @p texture and @p spread; false (with nothing changed) when
    // the list is full.
    [[nodiscard]] bool reserve(u32 quads, UiTextureId texture, f32 spread) const noexcept
    {
        if (quads == 0)
        {
            return true;
        }
        if (u64{vertices.size()} + u64{quads} * 4 > desc.maxVertices ||
            u64{indices.size()} + u64{quads} * 6 > desc.maxIndices)
        {
            return false;
        }
        const bool batched = !commands.empty() && commands.back().texture == texture &&
                             commands.back().sdfSpread == spread && commands.back().clip == clip;
        if (batched)
        {
            return true;
        }
        if (commands.size() >= desc.maxCommands)
        {
            return false;
        }
        UiDrawCommand command;
        command.texture = texture;
        command.sdfSpread = spread;
        command.clip = clip;
        command.firstIndex = static_cast<u32>(indices.size());
        commands.push_back(command);
        return true;
    }

    void add(const Quad& quad, u32 color) const noexcept
    {
        const auto base = static_cast<u32>(vertices.size());
        const std::array<UiVertex, 4> corners{
            UiVertex{quad.position[0], quad.uv[0], color}, UiVertex{quad.position[1], quad.uv[1], color},
            UiVertex{quad.position[2], quad.uv[2], color}, UiVertex{quad.position[3], quad.uv[3], color}};
        const std::array<u32, 6> triangles{base, base + 1, base + 2, base, base + 2, base + 3};
        vertices.insert(vertices.end(), corners.begin(), corners.end());
        indices.insert(indices.end(), triangles.begin(), triangles.end());
        commands.back().indexCount += 6;
    }

    // Places, reserves and adds @p quads as one primitive: all of them or none.
    [[nodiscard]] bool record(std::span<Quad> quads, UiTextureId texture, f32 spread,
                              u32 color) const noexcept
    {
        for (Quad& quad : quads)
        {
            if (!place(quad))
            {
                return false;
            }
        }
        if (!reserve(static_cast<u32>(quads.size()), texture, spread))
        {
            return false;
        }
        for (const Quad& quad : quads)
        {
            add(quad, color);
        }
        return true;
    }
};

bool validStyle(const UiTextStyle& style) noexcept
{
    return finite(style.size) && style.size > 0.0f && finite(style.lineSpacing) &&
           finite(style.letterSpacing) && finite(style.wrapWidth) && finite(style.color);
}

struct LineSpan
{
    usize end = 0;  // past the last byte drawn on the line
    usize next = 0; // first byte of the next line
    f32 width = 0.0f;
    bool newline = false; // the line ended at a line break
};

// Walks the glyphs of one line with the same arithmetic for measuring and drawing.
struct Pen
{
    Pen(const Font& textFont, const UiTextStyle& style) noexcept
        : font(textFont)
        , size(style.size)
        , letterSpacing(style.letterSpacing)
    {
    }

    const Font& font;
    f32 size;
    f32 letterSpacing;
    f32 x = 0.0f;
    char32_t previous = 0;
    bool first = true;

    struct Step
    {
        const FontGlyph* glyph;
        bool missing;
        f32 origin; // x of the glyph's origin
        f32 next;   // x after its advance
    };

    [[nodiscard]] Step peek(char32_t codePoint) const noexcept
    {
        const FontGlyph* found = font.find(codePoint);
        const FontGlyph& glyph = found != nullptr ? *found : font.glyphOrFallback(codePoint);
        const f32 origin = first ? x : x + (font.kerning(previous, glyph.codePoint) + letterSpacing) * size;
        return {&glyph, found == nullptr, origin, origin + glyph.advance * size};
    }

    void advance(const Step& step) noexcept
    {
        x = step.next;
        previous = step.glyph->codePoint;
        first = false;
    }
};

LineSpan findLine(const Font& font, std::string_view utf8, usize begin, const UiTextStyle& style) noexcept
{
    Pen pen(font, style);
    bool inSpaces = false;
    usize spacesBegin = 0;
    f32 spacesX = 0.0f;
    bool canBreak = false;
    LineSpan wrapped;
    usize offset = begin;
    while (offset < utf8.size())
    {
        const text::Utf8Decoded decoded = text::decodeUtf8(utf8, offset);
        if (decoded.codePoint == U'\n')
        {
            return {offset, offset + decoded.length, pen.x, true};
        }
        const bool space = decoded.codePoint == U' ';
        if (!space && inSpaces)
        {
            canBreak = true;
            wrapped = {spacesBegin, offset, spacesX, false};
            inSpaces = false;
        }
        const Pen::Step step = pen.peek(decoded.codePoint);
        if (style.wrapWidth > 0.0f && !space && !pen.first && step.next > style.wrapWidth)
        {
            return canBreak ? wrapped : LineSpan{offset, offset, pen.x, false};
        }
        if (space && !inSpaces)
        {
            inSpaces = true;
            spacesBegin = offset;
            spacesX = pen.x;
        }
        pen.advance(step);
        offset += decoded.length;
    }
    return {utf8.size(), utf8.size(), pen.x, false};
}

// Lays out @p utf8 and calls @p onGlyph(glyph, origin, missing) for every glyph, the origin relative to the
// top left of the first line. measureText() and UiDrawList::text() share it so that they cannot disagree.
template <class OnGlyph>
UiTextExtent layoutText(const Font& font, std::string_view utf8, const UiTextStyle& style,
                        OnGlyph&& onGlyph) noexcept
{
    if (utf8.empty() || font.empty())
    {
        return {};
    }
    const f32 lineStep = font.metrics().lineHeight() * style.size * style.lineSpacing;
    const f32 ascent = font.metrics().ascent * style.size;
    UiTextExtent extent;
    usize begin = 0;
    while (true)
    {
        const LineSpan line = findLine(font, utf8, begin, style);
        f32 lineX = 0.0f;
        if (style.align == UiTextAlign::Center)
        {
            lineX = style.wrapWidth > 0.0f ? (style.wrapWidth - line.width) * 0.5f : -line.width * 0.5f;
        }
        else if (style.align == UiTextAlign::End)
        {
            lineX = style.wrapWidth > 0.0f ? style.wrapWidth - line.width : -line.width;
        }
        const f32 baseline = ascent + static_cast<f32>(extent.lines) * lineStep;
        Pen pen(font, style);
        for (usize offset = begin; offset < line.end;)
        {
            const text::Utf8Decoded decoded = text::decodeUtf8(utf8, offset);
            const Pen::Step step = pen.peek(decoded.codePoint);
            onGlyph(*step.glyph, Vec2(lineX + step.origin, baseline), step.missing);
            pen.advance(step);
            offset += decoded.length;
        }
        ++extent.lines;
        extent.size.x = std::max(extent.size.x, line.width);
        if (line.next >= utf8.size())
        {
            if (line.newline)
            {
                ++extent.lines;
            }
            break;
        }
        begin = line.next;
    }
    extent.size.y = static_cast<f32>(extent.lines) * lineStep;
    return extent;
}

} // namespace

UiTextExtent measureText(const Font& font, std::string_view utf8, const UiTextStyle& style) noexcept
{
    if (!validStyle(style))
    {
        return {};
    }
    return layoutText(font, utf8, style, [](const FontGlyph&, Vec2, bool) {});
}

UiDrawList::UiDrawList(const UiDrawListDesc& desc)
    : m_desc(desc)
{
    m_vertices.reserve(desc.maxVertices);
    m_indices.reserve(desc.maxIndices);
    m_commands.reserve(desc.maxCommands);
    m_clips.reserve(usize{desc.maxClipDepth} + 1);
    begin(UiCanvasMetrics{});
}

void UiDrawList::begin(const UiCanvasMetrics& canvas) noexcept
{
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    m_clips.clear();
    m_canvas = canvas;
    m_clips.push_back(UiRect{Vec2(0.0f), canvas.size});
    m_transform = UiTransform2D{};
    m_identity = true;
    m_dropped = 0;
    m_missingGlyphs = 0;
}

bool UiDrawList::pushClip(const UiRect& rect) noexcept
{
    if (m_clips.size() > m_desc.maxClipDepth)
    {
        return false;
    }
    const UiRect& current = m_clips.back();
    m_clips.push_back(finite(rect) ? intersect(current, rect) : UiRect{current.min, current.min});
    return true;
}

void UiDrawList::popClip() noexcept
{
    ILL_ASSERT(m_clips.size() > 1, "UiDrawList::popClip() without a matching pushClip()");
    m_clips.pop_back();
}

const UiRect& UiDrawList::clip() const noexcept
{
    return m_clips.back();
}

void UiDrawList::setTransform(const UiTransform2D& transform) noexcept
{
    m_transform = transform;
    m_identity = transform == UiTransform2D{};
}

bool UiDrawList::rect(const UiRect& rect, Vec4 color) noexcept
{
    return image(rect, UiTextureId{}, color, UiRect{});
}

bool UiDrawList::image(const UiRect& rect, UiTextureId texture, Vec4 tint, const UiRect& uv) noexcept
{
    if (!finite(rect) || !finite(uv) || !finite(tint))
    {
        ++m_dropped;
        return false;
    }
    if (rect.empty())
    {
        return true;
    }
    const QuadSink sink{m_vertices, m_indices, m_commands, m_desc, m_transform, m_identity, m_clips.back()};
    std::array<Quad, 1> quad{makeQuad(rect, uv)};
    if (!sink.record(quad, texture, 0.0f, packColor(tint)))
    {
        ++m_dropped;
        return false;
    }
    return true;
}

bool UiDrawList::nineSlice(const UiRect& rect, UiTextureId texture, Vec2 textureSize, const UiEdges& border,
                           Vec4 tint, f32 borderScale) noexcept
{
    const bool validBorder = finite(border.left) && finite(border.top) && finite(border.right) &&
                             finite(border.bottom) && border.left >= 0.0f && border.top >= 0.0f &&
                             border.right >= 0.0f && border.bottom >= 0.0f;
    if (!finite(rect) || !finite(textureSize) || textureSize.x <= 0.0f || textureSize.y <= 0.0f ||
        !validBorder || !finite(tint) || !finite(borderScale) || borderScale < 0.0f)
    {
        ++m_dropped;
        return false;
    }
    if (rect.empty())
    {
        return true;
    }
    const Vec2 size = rect.size();
    Vec2 start(border.left * borderScale, border.top * borderScale);
    Vec2 end(border.right * borderScale, border.bottom * borderScale);
    for (glm::length_t axis = 0; axis < 2; ++axis)
    {
        const f32 total = start[axis] + end[axis];
        if (total > size[axis])
        {
            const f32 shrink = size[axis] / total;
            start[axis] *= shrink;
            end[axis] *= shrink;
        }
    }
    const std::array<f32, 4> xs{rect.min.x, rect.min.x + start.x, rect.max.x - end.x, rect.max.x};
    const std::array<f32, 4> ys{rect.min.y, rect.min.y + start.y, rect.max.y - end.y, rect.max.y};
    const std::array<f32, 4> us{0.0f, border.left / textureSize.x, 1.0f - border.right / textureSize.x, 1.0f};
    const std::array<f32, 4> vs{0.0f, border.top / textureSize.y, 1.0f - border.bottom / textureSize.y, 1.0f};
    std::array<Quad, 9> quads{};
    usize count = 0;
    for (usize row = 0; row < 3; ++row)
    {
        for (usize column = 0; column < 3; ++column)
        {
            if (xs[column + 1] > xs[column] && ys[row + 1] > ys[row])
            {
                quads[count++] = makeQuad({{xs[column], ys[row]}, {xs[column + 1], ys[row + 1]}},
                                          {{us[column], vs[row]}, {us[column + 1], vs[row + 1]}});
            }
        }
    }
    const QuadSink sink{m_vertices, m_indices, m_commands, m_desc, m_transform, m_identity, m_clips.back()};
    if (!sink.record(std::span(quads.data(), count), texture, 0.0f, packColor(tint)))
    {
        ++m_dropped;
        return false;
    }
    return true;
}

bool UiDrawList::text(const UiFontFace& face, Vec2 position, std::string_view utf8,
                      const UiTextStyle& style) noexcept
{
    if (utf8.empty())
    {
        return true;
    }
    if (face.font == nullptr || face.font->empty() || !finite(position) || !validStyle(style))
    {
        ++m_dropped;
        return false;
    }
    const Font& font = *face.font;
    const f32 pixelsPerUnit = m_canvas.pixelsPerUnit;
    const bool snap = m_identity && finite(pixelsPerUnit) && pixelsPerUnit > 0.0f;
    const QuadSink sink{m_vertices, m_indices, m_commands, m_desc, m_transform, m_identity, m_clips.back()};
    // The quad of a glyph with ink; false for glyphs without ink or outside the clip.
    bool valid = true;
    const auto glyphQuad = [&](const FontGlyph& glyph, Vec2 origin, Quad& quad) {
        if (glyph.planeMin == glyph.planeMax)
        {
            return false;
        }
        Vec2 at = position + origin;
        if (snap)
        {
            at = glm::round(at * pixelsPerUnit) / pixelsPerUnit;
        }
        quad = makeQuad({at + glyph.planeMin * style.size, at + glyph.planeMax * style.size},
                        {glyph.uvMin, glyph.uvMax});
        if (!sink.place(quad))
        {
            valid = false;
            return false;
        }
        return sink.visible(quad);
    };

    u32 quads = 0;
    layoutText(font, utf8, style, [&](const FontGlyph& glyph, Vec2 origin, bool) {
        Quad quad;
        if (glyphQuad(glyph, origin, quad))
        {
            ++quads;
        }
    });
    const f32 spread = static_cast<f32>(font.spread());
    if (!valid || !sink.reserve(quads, face.atlas, spread))
    {
        ++m_dropped;
        return false;
    }
    const u32 color = packColor(style.color);
    layoutText(font, utf8, style, [&](const FontGlyph& glyph, Vec2 origin, bool missing) {
        if (missing)
        {
            ++m_missingGlyphs;
        }
        Quad quad;
        if (glyphQuad(glyph, origin, quad))
        {
            sink.add(quad, color);
        }
    });
    return true;
}

UiDrawData UiDrawList::data() const noexcept
{
    UiDrawData result;
    result.vertices = m_vertices;
    result.indices = m_indices;
    result.commands = m_commands;
    result.canvas = m_canvas;
    return result;
}

u32 UiDrawList::droppedPrimitives() const noexcept
{
    return m_dropped;
}

u32 UiDrawList::missingGlyphs() const noexcept
{
    return m_missingGlyphs;
}

} // namespace illusion
