#pragma once

/// @file
/// The draw list of the game UI (ADR 0057): allocation-free recording of rectangles, images, nine-slice
/// panels and text as batched quads, which UiRenderer draws after the scene.
///
/// @code
/// illusion::UiDrawList list;
/// list.begin(illusion::canvasMetrics({}, targetSize));
/// list.rect({{24.0f, 24.0f}, {344.0f, 88.0f}}, {0.0f, 0.0f, 0.0f, 0.6f});
/// illusion::UiTextStyle style;
/// style.size = 32.0f;
/// list.text({&font, atlas}, {40.0f, 36.0f}, "Score 1200", style);
/// uiRenderer->render(commands, target, list.data());
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ui/UiGeometry.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

class Font;

/// Names a texture of the UiRenderer that draws the list (UiRenderer::addFont(), addImage()); 0 is none.
struct UiTextureId
{
    u32 value = 0; ///< The renderer's id; 0 for none (solid colour).

    /// Ids are equal when they name the same texture.
    friend bool operator==(const UiTextureId&, const UiTextureId&) noexcept = default;
};

/// One vertex of the UI (also the vertex buffer layout).
struct UiVertex
{
    Vec2 position; ///< Canvas units.
    Vec2 uv;       ///< Texture coordinate.
    u32 color = 0; ///< sRGB-encoded RGBA, straight alpha, red in the lowest byte.
};

/// A run of indices drawn with one texture and one clip rectangle.
struct UiDrawCommand
{
    UiTextureId texture{}; ///< Texture sampled; none for solid colour.
    /// Spread of the signed distance field in texels; 0 when the texture is a colour image.
    f32 sdfSpread = 0.0f;
    UiRect clip{};      ///< Pixels outside it are not drawn; canvas units.
    u32 firstIndex = 0; ///< First index in UiDrawData::indices.
    u32 indexCount = 0; ///< Number of indices, a multiple of 3.
};

/// What a UiDrawList recorded, borrowed: valid until the list's next begin().
struct UiDrawData
{
    std::span<const UiVertex> vertices{};      ///< Vertices of every command.
    std::span<const u32> indices{};            ///< Triangle list into vertices.
    std::span<const UiDrawCommand> commands{}; ///< Draw order: later commands are on top.
    UiCanvasMetrics canvas{};                  ///< The canvas the positions are in.
};

/// A Font with the texture its atlas has in the UiRenderer.
struct UiFontFace
{
    const Font* font = nullptr; ///< Glyphs and metrics.
    UiTextureId atlas{};        ///< UiRenderer::addFont() of that font.
};

/// Horizontal placement of the lines of a text.
enum class UiTextAlign : u8
{
    Start,  ///< Left edge at the position.
    Center, ///< Centred on the position, or in wrapWidth when set.
    End,    ///< Right edge at the position, or at wrapWidth when set.
};

/// How a text is laid out and coloured.
struct UiTextStyle
{
    f32 size = 24.0f;                       ///< Em size in canvas units.
    Vec4 color{1.0f};                       ///< Linear RGBA, straight alpha.
    UiTextAlign align = UiTextAlign::Start; ///< Horizontal placement of each line.
    f32 lineSpacing = 1.0f;                 ///< Multiplier of the font's line height.
    f32 letterSpacing = 0.0f;               ///< Extra advance per glyph in ems.
    /// Width in canvas units at which lines wrap (at spaces; a word longer than a line is split); 0 wraps
    /// only at line breaks.
    f32 wrapWidth = 0.0f;
};

/// Size of a laid-out text.
struct UiTextExtent
{
    Vec2 size{0.0f}; ///< Width of the widest line and height of all lines, in canvas units.
    u32 lines = 0;   ///< Number of lines; 0 for an empty text.
};

/// Lays out @p utf8 with @p style as UiDrawList::text() does and returns its size, without drawing. Invalid
/// UTF-8 sequences count as U+FFFD. Does not allocate.
[[nodiscard]] UiTextExtent measureText(const Font& font, std::string_view utf8,
                                       const UiTextStyle& style) noexcept;

/// Capacities of a UiDrawList, reserved at construction.
struct UiDrawListDesc
{
    u32 maxVertices = 65536; ///< Four per quad.
    u32 maxIndices = 98304;  ///< Six per quad.
    u32 maxCommands = 1024;  ///< Changes of texture or clip.
    u32 maxClipDepth = 32;   ///< Nested pushClip() calls.
};

/// Single-threaded, pre-reserved list of UI quads. Recording never allocates; a primitive that does not fit
/// (or has a non-finite coordinate) is dropped whole, returns false and is counted. Consecutive primitives
/// with the same texture and clip share a command.
class UiDrawList
{
public:
    /// Reserves the capacities of @p desc.
    explicit UiDrawList(const UiDrawListDesc& desc = {});

    /// Starts a frame on @p canvas: empties the list, resets the transform, the clip stack (to the whole
    /// canvas) and the counters.
    void begin(const UiCanvasMetrics& canvas) noexcept;

    /// Narrows the clip to its intersection with @p rect (canvas units, not transformed) until popClip().
    /// False when the stack is full; the clip is then unchanged and popClip() must not be called.
    bool pushClip(const UiRect& rect) noexcept;
    /// Restores the clip before the matching pushClip().
    void popClip() noexcept;
    /// The current clip rectangle.
    [[nodiscard]] const UiRect& clip() const noexcept;

    /// Transform applied to the positions of the primitives recorded from now on.
    void setTransform(const UiTransform2D& transform) noexcept;

    /// A rectangle of one colour (linear RGBA, straight alpha).
    bool rect(const UiRect& rect, Vec4 color) noexcept;
    /// @p texture stretched over @p rect, multiplied by @p tint; @p uv picks a part of it.
    bool image(const UiRect& rect, UiTextureId texture, Vec4 tint = Vec4(1.0f),
               const UiRect& uv = {{0.0f, 0.0f}, {1.0f, 1.0f}}) noexcept;
    /// A panel whose corners keep their size: @p border is in pixels of the texture (@p textureSize),
    /// drawn @p borderScale canvas units per pixel; edges stretch along one axis, the centre along both.
    /// Borders larger than the rectangle shrink to fit. Up to nine quads.
    bool nineSlice(const UiRect& rect, UiTextureId texture, Vec2 textureSize, const UiEdges& border,
                   Vec4 tint = Vec4(1.0f), f32 borderScale = 1.0f) noexcept;
    /// @p utf8 with its first line's top at @p position. Glyphs the font lacks draw its fallback glyph and
    /// are counted; glyph origins snap to whole pixels when the transform is the identity. Returns true for
    /// an empty text.
    bool text(const UiFontFace& face, Vec2 position, std::string_view utf8,
              const UiTextStyle& style) noexcept;

    /// What was recorded since begin().
    [[nodiscard]] UiDrawData data() const noexcept;
    /// Primitives dropped since begin() because the list was full or a value was invalid.
    [[nodiscard]] u32 droppedPrimitives() const noexcept;
    /// Code points drawn with the fallback glyph since begin().
    [[nodiscard]] u32 missingGlyphs() const noexcept;

private:
    std::vector<UiVertex> m_vertices;
    std::vector<u32> m_indices;
    std::vector<UiDrawCommand> m_commands;
    std::vector<UiRect> m_clips;
    UiDrawListDesc m_desc{};
    UiCanvasMetrics m_canvas{};
    UiTransform2D m_transform{};
    bool m_identity = true;
    u32 m_dropped = 0;
    u32 m_missingGlyphs = 0;
};

} // namespace illusion
