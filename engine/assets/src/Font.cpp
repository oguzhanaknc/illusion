#include <illusion/assets/Font.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Serialization.hpp>

// stb_truetype is compiled here with internal linkage, so it cannot clash with another copy of it (Dear ImGui
// carries one). Its header is a system header: its code is not subject to the engine's warning flags.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stb_truetype.h>
#include <utility>

namespace illusion
{
namespace
{

constexpr u32 kMagic = 0x4E464C49; // "ILFN"
constexpr u32 kVersion = 1;
constexpr char32_t kMaxCodePoint = 0x10FFFF;
constexpr u32 kGlyphGap = 1; // texels between packed glyphs, so that filtering never reads a neighbour
constexpr u32 kMinAtlasSide = 16;

bool validBakeSettings(const FontBakeSettings& settings) noexcept
{
    if (settings.pixelSize < 16 || settings.pixelSize > 128 || settings.spread < 2 || settings.spread > 16 ||
        settings.maxAtlasSize < 256 || settings.maxAtlasSize > 8192)
    {
        return false;
    }
    return std::ranges::all_of(settings.ranges, [](const CodePointRange& range) {
        return range.first <= range.last && range.last <= kMaxCodePoint;
    });
}

// Big-endian reads of the sfnt tables, bounds-checked against the whole file.
class FontBytes
{
public:
    explicit FontBytes(std::span<const std::byte> bytes) noexcept
        : m_bytes(bytes)
    {
    }

    [[nodiscard]] bool has(usize offset, usize size) const noexcept
    {
        return offset <= m_bytes.size() && size <= m_bytes.size() - offset;
    }
    [[nodiscard]] u32 u16At(usize offset) const noexcept
    {
        return (byteAt(offset) << 8u) | byteAt(offset + 1);
    }
    [[nodiscard]] u32 u32At(usize offset) const noexcept
    {
        return (u16At(offset) << 16u) | u16At(offset + 2);
    }

private:
    [[nodiscard]] u32 byteAt(usize offset) const noexcept { return static_cast<u32>(m_bytes[offset]); }

    std::span<const std::byte> m_bytes;
};

struct TableRange
{
    usize offset = 0;
    usize length = 0;
};

struct CheckedFont
{
    int faceOffset = 0;
    u32 numGlyphs = 0;
    TableRange glyf{};
    TableRange loca{};
    bool longLoca = false;
};

// Checks the structure that stb_truetype reads without bounds checks (it trusts its input): the table
// directory, the tables it requires, and the sizes of the metric, character map and location tables.
std::optional<CheckedFont> checkFontFile(std::span<const std::byte> file) noexcept
{
    const FontBytes bytes(file);
    if (file.size() > static_cast<usize>(std::numeric_limits<i32>::max()) || !bytes.has(0, 12))
    {
        return std::nullopt;
    }
    usize face = 0;
    if (bytes.u32At(0) == 0x74746366) // "ttcf": a collection; the first face is used
    {
        if (!bytes.has(0, 16) || bytes.u32At(8) == 0)
        {
            return std::nullopt;
        }
        face = bytes.u32At(12);
        if (!bytes.has(face, 12))
        {
            return std::nullopt;
        }
    }
    const u32 version = bytes.u32At(face);
    if (version != 0x00010000 && version != 0x74727565 && version != 0x4F54544F) // 1.0, "true", "OTTO"
    {
        return std::nullopt;
    }
    const usize tableCount = bytes.u16At(face + 4);
    if (!bytes.has(face + 12, tableCount * 16))
    {
        return std::nullopt;
    }
    const auto findTable = [&](u32 tag) -> std::optional<TableRange> {
        for (usize i = 0; i < tableCount; ++i)
        {
            const usize record = face + 12 + i * 16;
            if (bytes.u32At(record) == tag)
            {
                const TableRange range{bytes.u32At(record + 8), bytes.u32At(record + 12)};
                if (!bytes.has(range.offset, range.length))
                {
                    return std::nullopt;
                }
                return range;
            }
        }
        return std::nullopt;
    };
    const auto cmap = findTable(0x636D6170); // cmap
    const auto head = findTable(0x68656164); // head
    const auto hhea = findTable(0x68686561); // hhea
    const auto hmtx = findTable(0x686D7478); // hmtx
    const auto maxp = findTable(0x6D617870); // maxp
    const auto glyf = findTable(0x676C7966); // glyf
    const auto loca = findTable(0x6C6F6361); // loca
    const auto cff = findTable(0x43464620);  // "CFF "
    if (!cmap || !head || !hhea || !hmtx || !maxp || cmap->length < 4 || head->length < 54 ||
        hhea->length < 36 || maxp->length < 6 || (!(glyf && loca) && !cff))
    {
        return std::nullopt;
    }
    CheckedFont checked;
    checked.faceOffset = static_cast<int>(face);
    checked.numGlyphs = bytes.u16At(maxp->offset + 4);
    const usize longMetrics = bytes.u16At(hhea->offset + 34);
    if (checked.numGlyphs == 0 || longMetrics == 0 || hmtx->length < longMetrics * 4 ||
        bytes.u16At(head->offset) != 1 || bytes.u16At(head->offset + 18) == 0) // version 1, unitsPerEm
    {
        return std::nullopt;
    }
    const usize encodings = bytes.u16At(cmap->offset + 2);
    if (cmap->length < 4 + encodings * 8)
    {
        return std::nullopt;
    }
    for (usize i = 0; i < encodings; ++i)
    {
        const usize record = cmap->offset + 4 + i * 8;
        if (bytes.u32At(record + 4) + usize{4} > cmap->length)
        {
            return std::nullopt;
        }
    }
    if (glyf && loca)
    {
        checked.glyf = *glyf;
        checked.loca = *loca;
        checked.longLoca = bytes.u16At(head->offset + 50) != 0;
        const usize entry = checked.longLoca ? 4 : 2;
        if (loca->length < (usize{checked.numGlyphs} + 1) * entry)
        {
            return std::nullopt;
        }
    }
    return checked;
}

// True when the outline of a TrueType glyph lies inside the glyf table.
bool glyphInBounds(std::span<const std::byte> file, const CheckedFont& font, int glyph) noexcept
{
    if (glyph < 0 || static_cast<u32>(glyph) >= font.numGlyphs)
    {
        return false;
    }
    if (font.glyf.length == 0)
    {
        return true; // a CFF font
    }
    const FontBytes bytes(file);
    const usize index = static_cast<usize>(glyph);
    const usize start = font.longLoca ? bytes.u32At(font.loca.offset + index * 4)
                                      : usize{bytes.u16At(font.loca.offset + index * 2)} * 2;
    const usize end = font.longLoca ? bytes.u32At(font.loca.offset + index * 4 + 4)
                                    : usize{bytes.u16At(font.loca.offset + index * 2 + 2)} * 2;
    return start <= end && end <= font.glyf.length;
}

struct BakedGlyph
{
    FontGlyph glyph{};
    int glyphIndex = 0;
    UVec2 size{0};    // of the field bitmap in texels
    IVec2 offset{0};  // of the bitmap's top left from the origin, in texels
    usize pixels = 0; // offset of the bitmap in the scratch buffer
};

// Places the glyphs with ink on shelves of a square atlas of side @p side; false when they do not fit.
bool packShelves(std::span<BakedGlyph> glyphs, std::span<const u32> order, u32 side,
                 std::span<UVec2> positions) noexcept
{
    u32 x = 0;
    u32 y = 0;
    u32 shelfHeight = 0;
    for (const u32 index : order)
    {
        const UVec2 size = glyphs[index].size;
        if (size.x > side || size.y > side)
        {
            return false;
        }
        if (x + size.x > side)
        {
            y += shelfHeight + kGlyphGap;
            x = 0;
            shelfHeight = 0;
        }
        if (y + size.y > side)
        {
            return false;
        }
        positions[index] = UVec2(x, y);
        x += size.x + kGlyphGap;
        shelfHeight = std::max(shelfHeight, size.y);
    }
    return true;
}

bool finite(Vec2 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

void writeGlyph(BinaryWriter& writer, const FontGlyph& glyph)
{
    writer.write(static_cast<u32>(glyph.codePoint));
    writer.write(glyph.advance);
    for (const Vec2 v : {glyph.planeMin, glyph.planeMax, glyph.uvMin, glyph.uvMax})
    {
        writer.write(v.x);
        writer.write(v.y);
    }
}

bool readGlyph(BinaryReader& reader, FontGlyph& glyph) noexcept
{
    u32 codePoint = 0;
    reader.read(codePoint);
    reader.read(glyph.advance);
    for (Vec2* v : {&glyph.planeMin, &glyph.planeMax, &glyph.uvMin, &glyph.uvMax})
    {
        reader.read(v->x);
        reader.read(v->y);
    }
    glyph.codePoint = static_cast<char32_t>(codePoint);
    const auto inUnit = [](Vec2 v) {
        return v.x >= 0.0f && v.y >= 0.0f && v.x <= 1.0f && v.y <= 1.0f;
    };
    return reader.ok() && codePoint <= kMaxCodePoint && std::isfinite(glyph.advance) &&
           finite(glyph.planeMin) && finite(glyph.planeMax) && glyph.planeMin.x <= glyph.planeMax.x &&
           glyph.planeMin.y <= glyph.planeMax.y && inUnit(glyph.uvMin) && inUnit(glyph.uvMax) &&
           glyph.uvMin.x <= glyph.uvMax.x && glyph.uvMin.y <= glyph.uvMax.y;
}

constexpr usize kSerializedGlyphSize = 4 + 4 + 8 * 4;
constexpr usize kSerializedKerningSize = 8 + 4;

} // namespace

Expected<Font, FontError> Font::bake(std::span<const std::byte> fontFile, const FontBakeSettings& settings)
{
    ILL_PROFILE_SCOPE();
    if (!validBakeSettings(settings))
    {
        return unexpected(FontError::InvalidSettings);
    }
    const std::optional<CheckedFont> checked = checkFontFile(fontFile);
    stbtt_fontinfo info{};
    const auto* data = reinterpret_cast<const unsigned char*>(fontFile.data());
    if (!checked || stbtt_InitFont(&info, data, checked->faceOffset) == 0)
    {
        return unexpected(FontError::InvalidFont);
    }

    std::vector<BakedGlyph> baked;
    for (const CodePointRange& range : settings.ranges)
    {
        for (char32_t codePoint = range.first;; ++codePoint)
        {
            const int glyph = stbtt_FindGlyphIndex(&info, static_cast<int>(codePoint));
            if (glyph != 0 && glyphInBounds(fontFile, *checked, glyph))
            {
                BakedGlyph entry;
                entry.glyph.codePoint = codePoint;
                entry.glyphIndex = glyph;
                baked.push_back(entry);
            }
            if (codePoint == range.last)
            {
                break;
            }
        }
    }
    std::ranges::sort(baked, {}, [](const BakedGlyph& g) { return g.glyph.codePoint; });
    const auto duplicates =
        std::ranges::unique(baked, {}, [](const BakedGlyph& g) { return g.glyph.codePoint; });
    baked.erase(duplicates.begin(), duplicates.end());
    if (baked.empty())
    {
        return unexpected(FontError::NoGlyphs);
    }

    const f32 emPerUnit = stbtt_ScaleForMappingEmToPixels(&info, 1.0f);
    const f32 scale = emPerUnit * static_cast<f32>(settings.pixelSize);
    const f32 emPerTexel = 1.0f / static_cast<f32>(settings.pixelSize);
    const f32 distanceScale = 128.0f / static_cast<f32>(settings.spread);

    std::vector<u8> scratch;
    std::vector<u32> order;
    u64 inkArea = 0;
    u32 widest = 0;
    for (u32 i = 0; i < baked.size(); ++i)
    {
        BakedGlyph& entry = baked[i];
        int advance = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&info, entry.glyphIndex, &advance, &bearing);
        entry.glyph.advance = static_cast<f32>(advance) * emPerUnit;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        unsigned char* field =
            stbtt_GetGlyphSDF(&info, scale, entry.glyphIndex, static_cast<int>(settings.spread), 128,
                              distanceScale, &width, &height, &x, &y);
        if (field == nullptr)
        {
            continue;
        }
        if (width > 0 && height > 0)
        {
            entry.size = UVec2(static_cast<u32>(width), static_cast<u32>(height));
            entry.offset = IVec2(x, y);
            entry.pixels = scratch.size();
            scratch.insert(scratch.end(), field,
                           field + static_cast<usize>(width) * static_cast<usize>(height));
            order.push_back(i);
            inkArea += u64{entry.size.x + kGlyphGap} * (entry.size.y + kGlyphGap);
            widest = std::max({widest, entry.size.x, entry.size.y});
        }
        stbtt_FreeSDF(field, nullptr);
    }

    // Tallest first, then widest, then by code point: a total order, so the packing is the same everywhere.
    std::ranges::sort(order, [&](u32 a, u32 b) {
        const BakedGlyph& ga = baked[a];
        const BakedGlyph& gb = baked[b];
        if (ga.size.y != gb.size.y)
        {
            return ga.size.y > gb.size.y;
        }
        if (ga.size.x != gb.size.x)
        {
            return ga.size.x > gb.size.x;
        }
        return ga.glyph.codePoint < gb.glyph.codePoint;
    });
    std::vector<UVec2> positions(baked.size(), UVec2(0));
    u32 side = kMinAtlasSide;
    while (side < settings.maxAtlasSize && (side < widest || u64{side} * side < inkArea))
    {
        side *= 2;
    }
    while (!packShelves(baked, order, side, positions))
    {
        if (side * 2 > settings.maxAtlasSize)
        {
            return unexpected(FontError::AtlasFull);
        }
        side *= 2;
    }
    if (side > settings.maxAtlasSize)
    {
        return unexpected(FontError::AtlasFull);
    }

    Font font;
    font.m_pixelSize = settings.pixelSize;
    font.m_spread = settings.spread;
    font.m_atlasSize = UVec2(side, side);
    font.m_atlas.assign(usize{side} * side, 0);
    const f32 texel = 1.0f / static_cast<f32>(side);
    font.m_glyphs.reserve(baked.size());
    for (u32 i = 0; i < baked.size(); ++i)
    {
        BakedGlyph& entry = baked[i];
        if (entry.size.x > 0)
        {
            const UVec2 at = positions[i];
            for (u32 row = 0; row < entry.size.y; ++row)
            {
                std::memcpy(&font.m_atlas[(usize{at.y} + row) * side + at.x],
                            &scratch[entry.pixels + usize{row} * entry.size.x], entry.size.x);
            }
            entry.glyph.planeMin = Vec2(entry.offset) * emPerTexel;
            entry.glyph.planeMax = Vec2(entry.offset + IVec2(entry.size)) * emPerTexel;
            entry.glyph.uvMin = Vec2(at) * texel;
            entry.glyph.uvMax = Vec2(at + entry.size) * texel;
        }
        font.m_glyphs.push_back(entry.glyph);
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    font.m_metrics.ascent = static_cast<f32>(ascent) * emPerUnit;
    font.m_metrics.descent = static_cast<f32>(-descent) * emPerUnit;
    font.m_metrics.lineGap = static_cast<f32>(lineGap) * emPerUnit;

    const auto addPair = [&](char32_t left, char32_t right, int units) {
        if (units != 0)
        {
            font.m_kerning.push_back({(u64{left} << 32u) | u64{right}, static_cast<f32>(units) * emPerUnit});
        }
    };
    const int kernEntries = info.gpos == 0 ? stbtt_GetKerningTableLength(&info) : 0;
    if (kernEntries > 0)
    {
        // A kern table only: read it whole instead of asking for every pair.
        std::vector<stbtt_kerningentry> table(static_cast<usize>(kernEntries));
        const int count = stbtt_GetKerningTable(&info, table.data(), kernEntries);
        std::vector<std::pair<int, char32_t>> byGlyph;
        byGlyph.reserve(baked.size());
        for (const BakedGlyph& entry : baked)
        {
            byGlyph.emplace_back(entry.glyphIndex, entry.glyph.codePoint);
        }
        std::ranges::sort(byGlyph);
        for (usize i = 0; i < static_cast<usize>(count); ++i)
        {
            const stbtt_kerningentry& pair = table[i];
            const auto lefts =
                std::ranges::equal_range(byGlyph, pair.glyph1, {}, &std::pair<int, char32_t>::first);
            const auto rights =
                std::ranges::equal_range(byGlyph, pair.glyph2, {}, &std::pair<int, char32_t>::first);
            for (const auto& left : lefts)
            {
                for (const auto& right : rights)
                {
                    addPair(left.second, right.second, pair.advance);
                }
            }
        }
    }
    else if (info.gpos != 0 || info.kern != 0)
    {
        for (const BakedGlyph& left : baked)
        {
            for (const BakedGlyph& right : baked)
            {
                addPair(left.glyph.codePoint, right.glyph.codePoint,
                        stbtt_GetGlyphKernAdvance(&info, left.glyphIndex, right.glyphIndex));
            }
        }
    }
    std::ranges::sort(font.m_kerning, {}, &KerningPair::pair);
    const auto repeated = std::ranges::unique(font.m_kerning, {}, &KerningPair::pair);
    font.m_kerning.erase(repeated.begin(), repeated.end());

    const FontGlyph* fallback = font.find(U'\uFFFD');
    if (fallback == nullptr)
    {
        fallback = font.find(U'?');
    }
    font.m_fallback = fallback == nullptr ? 0 : static_cast<u32>(fallback - font.m_glyphs.data());
    return font;
}

const FontGlyph* Font::find(char32_t codePoint) const noexcept
{
    const auto it = std::ranges::lower_bound(m_glyphs, codePoint, {}, &FontGlyph::codePoint);
    return it != m_glyphs.end() && it->codePoint == codePoint ? &*it : nullptr;
}

const FontGlyph& Font::glyphOrFallback(char32_t codePoint) const noexcept
{
    ILL_ASSERT(!m_glyphs.empty(), "Font::glyphOrFallback() on an empty font");
    const FontGlyph* glyph = find(codePoint);
    return glyph != nullptr ? *glyph : m_glyphs[m_fallback];
}

f32 Font::kerning(char32_t left, char32_t right) const noexcept
{
    const u64 key = (u64{left} << 32u) | u64{right};
    const auto it = std::ranges::lower_bound(m_kerning, key, {}, &KerningPair::pair);
    return it != m_kerning.end() && it->pair == key ? it->amount : 0.0f;
}

std::vector<std::byte> serializeFont(const Font& font)
{
    std::vector<std::byte> bytes;
    bytes.reserve(64 + font.m_glyphs.size() * kSerializedGlyphSize +
                  font.m_kerning.size() * kSerializedKerningSize + font.m_atlas.size());
    BinaryWriter writer(bytes);
    writer.write(kMagic);
    writer.write(kVersion);
    writer.write(font.m_pixelSize);
    writer.write(font.m_spread);
    writer.write(font.m_metrics.ascent);
    writer.write(font.m_metrics.descent);
    writer.write(font.m_metrics.lineGap);
    writer.write(font.m_atlasSize.x);
    writer.write(font.m_atlasSize.y);
    writer.write(font.m_fallback);
    writer.write(static_cast<u64>(font.m_glyphs.size()));
    for (const FontGlyph& glyph : font.m_glyphs)
    {
        writeGlyph(writer, glyph);
    }
    writer.write(static_cast<u64>(font.m_kerning.size()));
    for (const Font::KerningPair& pair : font.m_kerning)
    {
        writer.write(pair.pair);
        writer.write(pair.amount);
    }
    writer.writeVector(std::span<const u8>(font.m_atlas));
    return bytes;
}

std::optional<Font> deserializeFont(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    u32 magic = 0;
    u32 version = 0;
    if (!reader.read(magic) || magic != kMagic || !reader.read(version) || version != kVersion)
    {
        return std::nullopt;
    }
    Font font;
    reader.read(font.m_pixelSize);
    reader.read(font.m_spread);
    reader.read(font.m_metrics.ascent);
    reader.read(font.m_metrics.descent);
    reader.read(font.m_metrics.lineGap);
    reader.read(font.m_atlasSize.x);
    reader.read(font.m_atlasSize.y);
    reader.read(font.m_fallback);
    u64 glyphCount = 0;
    reader.read(glyphCount);
    if (!reader.ok() || font.m_pixelSize < 16 || font.m_pixelSize > 128 || font.m_spread < 2 ||
        font.m_spread > 16 || !std::isfinite(font.m_metrics.ascent) ||
        !std::isfinite(font.m_metrics.descent) || !std::isfinite(font.m_metrics.lineGap) ||
        font.m_atlasSize.x == 0 || font.m_atlasSize.y == 0 || font.m_atlasSize.x > 8192 ||
        font.m_atlasSize.y > 8192 || glyphCount == 0 ||
        glyphCount > reader.remaining() / kSerializedGlyphSize || font.m_fallback >= glyphCount)
    {
        return std::nullopt;
    }
    font.m_glyphs.resize(static_cast<usize>(glyphCount));
    for (usize i = 0; i < font.m_glyphs.size(); ++i)
    {
        if (!readGlyph(reader, font.m_glyphs[i]) ||
            (i > 0 && font.m_glyphs[i].codePoint <= font.m_glyphs[i - 1].codePoint))
        {
            return std::nullopt;
        }
    }
    u64 pairCount = 0;
    if (!reader.read(pairCount) || pairCount > reader.remaining() / kSerializedKerningSize)
    {
        return std::nullopt;
    }
    font.m_kerning.resize(static_cast<usize>(pairCount));
    for (usize i = 0; i < font.m_kerning.size(); ++i)
    {
        Font::KerningPair& pair = font.m_kerning[i];
        reader.read(pair.pair);
        reader.read(pair.amount);
        if (!reader.ok() || !std::isfinite(pair.amount) ||
            (i > 0 && pair.pair <= font.m_kerning[i - 1].pair) ||
            font.find(static_cast<char32_t>(pair.pair >> 32u)) == nullptr ||
            font.find(static_cast<char32_t>(pair.pair & 0xFFFFFFFFu)) == nullptr)
        {
            return std::nullopt;
        }
    }
    if (!reader.readVector(font.m_atlas) ||
        font.m_atlas.size() != usize{font.m_atlasSize.x} * font.m_atlasSize.y || reader.remaining() != 0)
    {
        return std::nullopt;
    }
    return font;
}

} // namespace illusion
