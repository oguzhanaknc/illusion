#pragma once

/// @file
/// Fonts for the game UI (ADR 0057): a TrueType or OpenType file baked into one signed distance field
/// atlas with glyph metrics and kerning, drawable at any size.
///
/// @code
/// auto font = illusion::Font::bake(fileBytes);
/// if (font)
/// {
///     const illusion::FontGlyph& glyph = font->glyphOrFallback(U'A');
///     const f32 advancePixels = glyph.advance * 32.0f; // at a size of 32
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace illusion
{

/// An inclusive range of Unicode code points.
struct CodePointRange
{
    char32_t first = 0; ///< First code point.
    char32_t last = 0;  ///< Last code point, not below first.

    /// Ranges are equal when both ends are.
    friend bool operator==(const CodePointRange&, const CodePointRange&) noexcept = default;
};

/// The ranges baked by default: Basic Latin, Latin-1, Latin Extended-A (Turkish and most European
/// languages), general punctuation, and the euro and trade mark signs.
inline constexpr CodePointRange kDefaultFontRanges[] = {
    {0x0020, 0x007E}, {0x00A0, 0x017F}, {0x2010, 0x2027}, {0x20AC, 0x20AC}, {0x2122, 0x2122}};

/// How Font::bake() makes the atlas.
struct FontBakeSettings
{
    u32 pixelSize = 48; ///< Pixels per em in the atlas, 16 to 128.
    u32 spread = 6;     ///< Distance in atlas pixels that the field covers on each side of an edge, 2 to 16.
    /// Code points to bake; those the font lacks are left out.
    std::span<const CodePointRange> ranges = kDefaultFontRanges;
    u32 maxAtlasSize = 2048; ///< Largest atlas side in pixels, 256 to 8192.
};

/// Why a font could not be baked.
enum class FontError : u8
{
    InvalidFont,     ///< Not a TrueType or OpenType file that can be read.
    InvalidSettings, ///< A setting is out of its range, or a range is inverted.
    NoGlyphs,        ///< The font has none of the requested code points.
    AtlasFull,       ///< The glyphs do not fit into maxAtlasSize.
};

/// One glyph of a Font. Lengths are in ems (multiply by the text size); y points down from the baseline.
struct FontGlyph
{
    char32_t codePoint = 0; ///< The code point this glyph draws.
    f32 advance = 0.0f;     ///< Distance to the next glyph's origin.
    Vec2 planeMin{0.0f};    ///< Top left of the quad relative to the origin, with the field's spread.
    Vec2 planeMax{0.0f};    ///< Bottom right of the quad; equal to planeMin for glyphs without ink.
    Vec2 uvMin{0.0f};       ///< Atlas coordinate of planeMin.
    Vec2 uvMax{0.0f};       ///< Atlas coordinate of planeMax.
};

/// Vertical metrics of a Font in ems.
struct FontMetrics
{
    f32 ascent = 0.0f;  ///< Baseline to the top of a line, positive.
    f32 descent = 0.0f; ///< Baseline to the bottom of a line, positive.
    f32 lineGap = 0.0f; ///< Extra space between lines.

    /// Distance between the baselines of two lines.
    [[nodiscard]] f32 lineHeight() const noexcept { return ascent + descent + lineGap; }
};

/// A baked font: an 8-bit single-channel signed distance field atlas (edge at 128, inside brighter), the
/// glyphs sorted by code point and pair kerning. Immutable after bake(); lookups do not allocate.
class Font
{
public:
    /// An empty font: no glyphs, find() returns null.
    Font() = default;

    /// Bakes @p fontFile (the first face of a TrueType or OpenType file). The same bytes and settings give
    /// the same glyphs, metrics and atlas layout everywhere; distance values may differ by a step between
    /// compilers (the field is computed in floating point).
    [[nodiscard]] static Expected<Font, FontError> bake(std::span<const std::byte> fontFile,
                                                        const FontBakeSettings& settings = {});

    /// The glyph of @p codePoint; null when it was not baked.
    [[nodiscard]] const FontGlyph* find(char32_t codePoint) const noexcept;
    /// The glyph of @p codePoint, or the fallback: U+FFFD, else '?', else the first glyph. The font must
    /// not be empty.
    [[nodiscard]] const FontGlyph& glyphOrFallback(char32_t codePoint) const noexcept;
    /// Adjustment in ems of the advance between @p left and @p right; 0 for pairs without kerning.
    [[nodiscard]] f32 kerning(char32_t left, char32_t right) const noexcept;

    /// True when the font has no glyphs.
    [[nodiscard]] bool empty() const noexcept { return m_glyphs.empty(); }
    /// Every glyph, sorted by code point.
    [[nodiscard]] std::span<const FontGlyph> glyphs() const noexcept { return m_glyphs; }
    /// Vertical metrics.
    [[nodiscard]] const FontMetrics& metrics() const noexcept { return m_metrics; }
    /// Atlas width and height in pixels.
    [[nodiscard]] UVec2 atlasSize() const noexcept { return m_atlasSize; }
    /// Atlas pixels, row by row from the top, one byte each.
    [[nodiscard]] std::span<const u8> atlas() const noexcept { return m_atlas; }
    /// Pixels per em the atlas was baked at.
    [[nodiscard]] u32 pixelSize() const noexcept { return m_pixelSize; }
    /// The field's spread in atlas pixels.
    [[nodiscard]] u32 spread() const noexcept { return m_spread; }

private:
    friend std::vector<std::byte> serializeFont(const Font& font);
    friend std::optional<Font> deserializeFont(std::span<const std::byte> bytes);

    struct KerningPair
    {
        u64 pair = 0; // left << 32 | right, sorted
        f32 amount = 0.0f;
    };

    std::vector<FontGlyph> m_glyphs;
    std::vector<KerningPair> m_kerning;
    std::vector<u8> m_atlas;
    FontMetrics m_metrics{};
    UVec2 m_atlasSize{0};
    u32 m_pixelSize = 0;
    u32 m_spread = 0;
    u32 m_fallback = 0; // index into m_glyphs
};

/// Writes @p font as the payload of a cooked asset (AssetType::Font).
[[nodiscard]] std::vector<std::byte> serializeFont(const Font& font);

/// Reads a font written by serializeFont(); std::nullopt for invalid data.
[[nodiscard]] std::optional<Font> deserializeFont(std::span<const std::byte> bytes);

} // namespace illusion
