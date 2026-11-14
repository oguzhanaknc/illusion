#include "AssetImport.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.import";
constexpr u32 kMaxTextureSize = 16384;
constexpr f64 kMaxModelScale = 1000.0;
constexpr f64 kMinLodReduction = 0.1;
constexpr f64 kMaxLodReduction = 0.9;
constexpr u64 kMaxCodePoint = 0x10FFFF;

constexpr std::array<std::string_view, 3> kColorSpaceNames{"auto", "srgb", "linear"};
constexpr std::array<std::string_view, 6> kCompressionNames{"auto", "none", "bc7", "bc5", "bc4", "bc6h"};
constexpr std::array<std::string_view, 3> kQualityNames{"fast", "normal", "high"};

bool endsWithNoCase(std::string_view text, std::string_view suffix) noexcept
{
    return text.size() >= suffix.size() &&
           std::equal(suffix.begin(), suffix.end(), text.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

ImportSettingsError fail(std::string pointer, std::string message)
{
    return ImportSettingsError{std::move(pointer), std::move(message)};
}

// The index of @p value in @p names, or std::nullopt if it is not a string or not one of them.
template <usize N>
std::optional<usize> nameIndex(const JsonValue& value, const std::array<std::string_view, N>& names)
{
    const std::optional<std::string_view> name = value.asString();
    const auto found = name ? std::find(names.begin(), names.end(), *name) : names.end();
    if (found == names.end())
    {
        return std::nullopt;
    }
    return static_cast<usize>(found - names.begin());
}

std::string_view sectionName(ImportSettingsKind kind) noexcept
{
    switch (kind)
    {
    case ImportSettingsKind::Texture:
        return "texture";
    case ImportSettingsKind::Font:
        return "font";
    default:
        return "model";
    }
}

Expected<void, ImportSettingsError> mergeTexture(TextureImportSettings& texture, const JsonValue& patch)
{
    if (patch.isNull())
    {
        texture = {};
        return {};
    }
    if (!patch.isObject())
    {
        return unexpected(fail("/texture", "must be an object or null"));
    }
    TextureImportSettings merged = texture;
    for (const JsonMember member : patch.members())
    {
        const std::string pointer = std::format("/texture/{}", member.key);
        const JsonValue& value = member.value;
        if (member.key == "colorSpace")
        {
            if (value.isNull())
            {
                merged.colorSpace = TextureImportSettings{}.colorSpace;
                continue;
            }
            const std::optional<usize> index = nameIndex(value, kColorSpaceNames);
            if (!index)
            {
                return unexpected(fail(pointer, "must be \"auto\", \"srgb\" or \"linear\""));
            }
            merged.colorSpace = static_cast<ImportColorSpace>(*index);
        }
        else if (member.key == "generateMips")
        {
            if (value.isNull())
            {
                merged.generateMips = TextureImportSettings{}.generateMips;
                continue;
            }
            const std::optional<bool> flag = value.asBool();
            if (!flag)
            {
                return unexpected(fail(pointer, "must be true or false"));
            }
            merged.generateMips = *flag;
        }
        else if (member.key == "maxSize")
        {
            if (value.isNull())
            {
                merged.maxSize = TextureImportSettings{}.maxSize;
                continue;
            }
            const std::optional<u64> size = value.isInteger() ? value.asU64() : std::nullopt;
            if (!size || *size > kMaxTextureSize)
            {
                return unexpected(fail(
                    pointer, std::format("must be an integer from 0 (no limit) to {}", kMaxTextureSize)));
            }
            merged.maxSize = static_cast<u32>(*size);
        }
        else if (member.key == "compression")
        {
            if (value.isNull())
            {
                merged.compression = TextureImportSettings{}.compression;
                continue;
            }
            const std::optional<usize> index = nameIndex(value, kCompressionNames);
            if (!index)
            {
                return unexpected(
                    fail(pointer, "must be \"auto\", \"none\", \"bc7\", \"bc5\", \"bc4\" or \"bc6h\""));
            }
            merged.compression = static_cast<ImportCompression>(*index);
        }
        else if (member.key == "quality")
        {
            if (value.isNull())
            {
                merged.quality = TextureImportSettings{}.quality;
                continue;
            }
            const std::optional<usize> index = nameIndex(value, kQualityNames);
            if (!index)
            {
                return unexpected(fail(pointer, "must be \"fast\", \"normal\" or \"high\""));
            }
            merged.quality = static_cast<TextureQuality>(*index);
        }
        else
        {
            return unexpected(
                fail(pointer,
                     "is not a texture setting (colorSpace, generateMips, maxSize, compression, quality)"));
        }
    }
    texture = merged;
    return {};
}

Expected<void, ImportSettingsError> mergeModel(ModelImportSettings& model, const JsonValue& patch)
{
    if (patch.isNull())
    {
        model = {};
        return {};
    }
    if (!patch.isObject())
    {
        return unexpected(fail("/model", "must be an object or null"));
    }
    ModelImportSettings merged = model;
    const ModelImportSettings defaults{};
    for (const JsonMember member : patch.members())
    {
        const std::string pointer = std::format("/model/{}", member.key);
        const JsonValue value = member.value;
        if (member.key == "scale")
        {
            if (value.isNull())
            {
                merged.scale = defaults.scale;
                continue;
            }
            const std::optional<f64> scale = value.asF64();
            if (!scale || !(*scale > 0.0) || *scale > kMaxModelScale)
            {
                return unexpected(fail(pointer, "must be a number greater than 0 and at most 1000"));
            }
            merged.scale = static_cast<f32>(*scale);
        }
        else if (member.key == "lodCount")
        {
            if (value.isNull())
            {
                merged.lodCount = defaults.lodCount;
                continue;
            }
            const std::optional<u64> count = value.isInteger() ? value.asU64() : std::nullopt;
            if (!count || *count > kMaxSubmeshLods)
            {
                return unexpected(
                    fail(pointer, std::format("must be an integer from 0 to {}", kMaxSubmeshLods)));
            }
            merged.lodCount = static_cast<u32>(*count);
        }
        else if (member.key == "lodReduction")
        {
            if (value.isNull())
            {
                merged.lodReduction = defaults.lodReduction;
                continue;
            }
            const std::optional<f64> reduction = value.asF64();
            if (!reduction || !(*reduction >= kMinLodReduction && *reduction <= kMaxLodReduction))
            {
                return unexpected(fail(pointer, "must be a number from 0.1 to 0.9"));
            }
            merged.lodReduction = static_cast<f32>(*reduction);
        }
        else if (member.key == "compressTextures")
        {
            if (value.isNull())
            {
                merged.compressTextures = defaults.compressTextures;
                continue;
            }
            const std::optional<bool> flag = value.asBool();
            if (!flag)
            {
                return unexpected(fail(pointer, "must be true or false"));
            }
            merged.compressTextures = *flag;
        }
        else if (member.key == "textureQuality")
        {
            if (value.isNull())
            {
                merged.textureQuality = defaults.textureQuality;
                continue;
            }
            const std::optional<usize> index = nameIndex(value, kQualityNames);
            if (!index)
            {
                return unexpected(fail(pointer, "must be \"fast\", \"normal\" or \"high\""));
            }
            merged.textureQuality = static_cast<TextureQuality>(*index);
        }
        else
        {
            return unexpected(fail(pointer, "is not a model setting (scale, lodCount, lodReduction, "
                                            "compressTextures, textureQuality)"));
        }
    }
    model = merged;
    return {};
}

// A u32 in [@p min, @p max] from @p value, or std::nullopt.
std::optional<u32> integerIn(const JsonValue& value, u64 min, u64 max)
{
    const std::optional<u64> number = value.isInteger() ? value.asU64() : std::nullopt;
    if (!number || *number < min || *number > max)
    {
        return std::nullopt;
    }
    return static_cast<u32>(*number);
}

// The ranges of @p value: a non-empty array of at most kMaxFontImportRanges `[first, last]` pairs of code
// points with first <= last.
Expected<std::vector<CodePointRange>, ImportSettingsError> parseFontRanges(const JsonValue& value,
                                                                           const std::string& pointer)
{
    if (!value.isArray() || value.size() == 0 || value.size() > kMaxFontImportRanges)
    {
        return unexpected(fail(pointer, "must be a list of 1 to 64 [first, last] code point pairs"));
    }
    std::vector<CodePointRange> ranges;
    ranges.reserve(value.size());
    for (usize i = 0; i < value.size(); ++i)
    {
        const JsonValue pair = value[i];
        const std::string at = std::format("{}/{}", pointer, i);
        if (!pair.isArray() || pair.size() != 2)
        {
            return unexpected(fail(at, "must be a [first, last] pair"));
        }
        const std::optional<u32> first = integerIn(pair[0], 0, kMaxCodePoint);
        const std::optional<u32> last = integerIn(pair[1], 0, kMaxCodePoint);
        if (!first || !last || *first > *last)
        {
            return unexpected(fail(at, "must be two code points from 0 to 1114111 with first <= last"));
        }
        ranges.push_back(CodePointRange{static_cast<char32_t>(*first), static_cast<char32_t>(*last)});
    }
    return ranges;
}

Expected<void, ImportSettingsError> mergeFont(FontImportSettings& font, const JsonValue& patch)
{
    if (patch.isNull())
    {
        font = {};
        return {};
    }
    if (!patch.isObject())
    {
        return unexpected(fail("/font", "must be an object or null"));
    }
    FontImportSettings merged = font;
    const FontImportSettings defaults{};
    for (const JsonMember member : patch.members())
    {
        const std::string pointer = std::format("/font/{}", member.key);
        const JsonValue value = member.value;
        if (member.key == "pixelSize")
        {
            const std::optional<u32> size = value.isNull() ? defaults.pixelSize : integerIn(value, 16, 128);
            if (!size)
            {
                return unexpected(fail(pointer, "must be an integer from 16 to 128"));
            }
            merged.pixelSize = *size;
        }
        else if (member.key == "spread")
        {
            const std::optional<u32> spread = value.isNull() ? defaults.spread : integerIn(value, 2, 16);
            if (!spread)
            {
                return unexpected(fail(pointer, "must be an integer from 2 to 16"));
            }
            merged.spread = *spread;
        }
        else if (member.key == "ranges")
        {
            if (value.isNull())
            {
                merged.ranges = defaults.ranges;
                continue;
            }
            Expected<std::vector<CodePointRange>, ImportSettingsError> ranges =
                parseFontRanges(value, pointer);
            if (!ranges)
            {
                return unexpected(std::move(ranges).error());
            }
            merged.ranges = std::move(*ranges);
        }
        else
        {
            return unexpected(fail(pointer, "is not a font setting (pixelSize, spread, ranges)"));
        }
    }
    font = std::move(merged);
    return {};
}

// Merges the section @p member ("texture", "model" or "font") into @p settings; only the section of @p kind
// exists.
Expected<void, ImportSettingsError> mergeSection(ImportSettings& settings, const JsonMember& member,
                                                 ImportSettingsKind kind)
{
    if (member.key != sectionName(kind))
    {
        return unexpected(
            fail(std::format("/{}", member.key),
                 member.key == "texture" || member.key == "model" || member.key == "font"
                     ? std::format("does not apply to this asset (it has \"{}\" settings)", sectionName(kind))
                     : std::format("is not a settings section (\"{}\")", sectionName(kind))));
    }
    switch (kind)
    {
    case ImportSettingsKind::Texture:
        return mergeTexture(settings.texture, member.value);
    case ImportSettingsKind::Font:
        return mergeFont(settings.font, member.value);
    default:
        return mergeModel(settings.model, member.value);
    }
}

// @p ranges as an array of [first, last] pairs.
JsonMutValue fontRangesValue(JsonMutDocument& document, const std::vector<CodePointRange>& ranges)
{
    JsonMutValue list = document.makeArray();
    for (const CodePointRange& range : ranges)
    {
        JsonMutValue pair = document.makeArray();
        pair.push(document.makeU64(range.first));
        pair.push(document.makeU64(range.last));
        list.push(pair);
    }
    return list;
}

} // namespace

FontBakeSettings fontBakeSettings(const FontImportSettings& settings) noexcept
{
    FontBakeSettings bake;
    bake.pixelSize = settings.pixelSize;
    bake.spread = settings.spread;
    bake.ranges = settings.ranges;
    return bake;
}

ImportSettingsKind importSettingsKindOf(std::string_view path) noexcept
{
    constexpr std::array<std::string_view, 6> kImages = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"};
    if (isImportSettingsPath(path))
    {
        return ImportSettingsKind::None;
    }
    for (const std::string_view extension : kImages)
    {
        if (endsWithNoCase(path, extension))
        {
            return ImportSettingsKind::Texture;
        }
    }
    if (endsWithNoCase(path, ".gltf") || endsWithNoCase(path, ".glb"))
    {
        return ImportSettingsKind::Model;
    }
    if (endsWithNoCase(path, ".ttf") || endsWithNoCase(path, ".otf"))
    {
        return ImportSettingsKind::Font;
    }
    return ImportSettingsKind::None;
}

bool isImportSettingsPath(std::string_view path) noexcept
{
    return path.size() > kImportSettingsSuffix.size() && endsWithNoCase(path, kImportSettingsSuffix);
}

std::string importSettingsPath(std::string_view assetPath)
{
    std::string path(assetPath);
    path += kImportSettingsSuffix;
    return path;
}

std::string_view importSettingsOwner(std::string_view sidecarPath) noexcept
{
    ILL_ASSERT(isImportSettingsPath(sidecarPath), "not an import settings sidecar path");
    return sidecarPath.substr(0, sidecarPath.size() - kImportSettingsSuffix.size());
}

Expected<void, ImportSettingsError> mergeImportSettings(ImportSettings& settings, const JsonValue& patch,
                                                        ImportSettingsKind kind)
{
    if (kind == ImportSettingsKind::None)
    {
        return unexpected(
            fail("", "this kind of asset has no import settings (only textures, models and fonts)"));
    }
    if (patch.isNull())
    {
        settings = {};
        return {};
    }
    if (!patch.isObject())
    {
        return unexpected(
            fail("", std::format("must be an object, e.g. {{\"{}\": {{...}}}}", sectionName(kind))));
    }
    ImportSettings merged = settings;
    for (const JsonMember member : patch.members())
    {
        if (Expected<void, ImportSettingsError> section = mergeSection(merged, member, kind); !section)
        {
            return section;
        }
    }
    settings = merged;
    return {};
}

Expected<ImportSettings, ImportSettingsError> parseImportSettings(std::string_view text,
                                                                  ImportSettingsKind kind)
{
    Expected<JsonDocument, JsonError> document = JsonDocument::parse(text);
    if (!document)
    {
        return unexpected(
            fail("", std::format("invalid JSON at line {}, column {}: {}", document.error().line,
                                 document.error().column, document.error().message)));
    }
    const JsonValue root = document->root();
    if (!root.isObject() || root.find("format").asString() != kFormatName)
    {
        return unexpected(
            fail("/format", "not an import settings file (missing \"format\": \"illusion.import\")"));
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > kImportSettingsVersion)
    {
        return unexpected(
            fail("/version", std::format("unsupported import settings version (this build reads 1 "
                                         "to {})",
                                         kImportSettingsVersion)));
    }
    if (kind == ImportSettingsKind::None)
    {
        return unexpected(
            fail("", "this kind of asset has no import settings (only textures, models and fonts)"));
    }
    ImportSettings settings;
    for (const JsonMember member : root.members())
    {
        if (member.key == "format" || member.key == "version")
        {
            continue;
        }
        if (Expected<void, ImportSettingsError> section = mergeSection(settings, member, kind); !section)
        {
            return unexpected(std::move(section).error());
        }
    }
    return settings;
}

std::string writeImportSettings(const ImportSettings& settings, ImportSettingsKind kind)
{
    const TextureImportSettings defaultTexture;
    const ModelImportSettings defaultModel;
    JsonMutDocument document;
    JsonMutValue root = document.makeObject();
    root.add("format", document.makeString(kFormatName));
    root.add("version", document.makeU64(kImportSettingsVersion));
    bool any = false;
    if (kind == ImportSettingsKind::Texture && settings.texture != defaultTexture)
    {
        JsonMutValue texture = root.addObject("texture");
        if (settings.texture.colorSpace != defaultTexture.colorSpace)
        {
            texture.add("colorSpace", document.makeString(
                                          kColorSpaceNames[static_cast<usize>(settings.texture.colorSpace)]));
        }
        if (settings.texture.generateMips != defaultTexture.generateMips)
        {
            texture.add("generateMips", document.makeBool(settings.texture.generateMips));
        }
        if (settings.texture.maxSize != defaultTexture.maxSize)
        {
            texture.add("maxSize", document.makeU64(settings.texture.maxSize));
        }
        if (settings.texture.compression != defaultTexture.compression)
        {
            texture.add(
                "compression",
                document.makeString(kCompressionNames[static_cast<usize>(settings.texture.compression)]));
        }
        if (settings.texture.quality != defaultTexture.quality)
        {
            texture.add("quality",
                        document.makeString(kQualityNames[static_cast<usize>(settings.texture.quality)]));
        }
        any = true;
    }
    if (kind == ImportSettingsKind::Model && settings.model != defaultModel)
    {
        JsonMutValue model = root.addObject("model");
        if (settings.model.scale != defaultModel.scale)
        {
            model.add("scale", document.makeF32(settings.model.scale));
        }
        if (settings.model.lodCount != defaultModel.lodCount)
        {
            model.add("lodCount", document.makeU64(settings.model.lodCount));
        }
        if (settings.model.lodReduction != defaultModel.lodReduction)
        {
            model.add("lodReduction", document.makeF32(settings.model.lodReduction));
        }
        if (settings.model.compressTextures != defaultModel.compressTextures)
        {
            model.add("compressTextures", document.makeBool(settings.model.compressTextures));
        }
        if (settings.model.textureQuality != defaultModel.textureQuality)
        {
            model.add("textureQuality",
                      document.makeString(kQualityNames[static_cast<usize>(settings.model.textureQuality)]));
        }
        any = true;
    }
    const FontImportSettings defaultFont;
    if (kind == ImportSettingsKind::Font && settings.font != defaultFont)
    {
        JsonMutValue font = root.addObject("font");
        if (settings.font.pixelSize != defaultFont.pixelSize)
        {
            font.add("pixelSize", document.makeU64(settings.font.pixelSize));
        }
        if (settings.font.spread != defaultFont.spread)
        {
            font.add("spread", document.makeU64(settings.font.spread));
        }
        if (settings.font.ranges != defaultFont.ranges)
        {
            font.add("ranges", fontRangesValue(document, settings.font.ranges));
        }
        any = true;
    }
    if (!any)
    {
        return {};
    }
    document.setRoot(root);
    std::string text;
    if (!document.write(text))
    {
        return {};
    }
    return text;
}

std::string importSettingsValue(const ImportSettings& settings, ImportSettingsKind kind)
{
    if (kind == ImportSettingsKind::Texture)
    {
        return std::format("{{\"texture\":{{\"colorSpace\":\"{}\",\"generateMips\":{},\"maxSize\":{},"
                           "\"compression\":\"{}\",\"quality\":\"{}\"}}}}",
                           kColorSpaceNames[static_cast<usize>(settings.texture.colorSpace)],
                           settings.texture.generateMips, settings.texture.maxSize,
                           kCompressionNames[static_cast<usize>(settings.texture.compression)],
                           kQualityNames[static_cast<usize>(settings.texture.quality)]);
    }
    if (kind == ImportSettingsKind::Font)
    {
        JsonMutDocument document;
        JsonMutValue root = document.makeObject();
        JsonMutValue font = root.addObject("font");
        font.add("pixelSize", document.makeU64(settings.font.pixelSize));
        font.add("spread", document.makeU64(settings.font.spread));
        font.add("ranges", fontRangesValue(document, settings.font.ranges));
        document.setRoot(root);
        std::string text;
        if (document.write(text, JsonWriteOptions{.pretty = false}))
        {
            return text;
        }
    }
    if (kind == ImportSettingsKind::Model)
    {
        JsonMutDocument document;
        JsonMutValue root = document.makeObject();
        JsonMutValue model = root.addObject("model");
        model.add("scale", document.makeF32(settings.model.scale));
        model.add("lodCount", document.makeU64(settings.model.lodCount));
        model.add("lodReduction", document.makeF32(settings.model.lodReduction));
        model.add("compressTextures", document.makeBool(settings.model.compressTextures));
        model.add("textureQuality",
                  document.makeString(kQualityNames[static_cast<usize>(settings.model.textureQuality)]));
        document.setRoot(root);
        std::string text;
        if (document.write(text, JsonWriteOptions{.pretty = false}))
        {
            return text;
        }
    }
    return "{}";
}

ImageImportSettings effectiveImageImport(const ImageImportSettings& requested,
                                         const TextureImportSettings& settings) noexcept
{
    ImageImportSettings effective = requested;
    if (settings.colorSpace != ImportColorSpace::Auto)
    {
        effective.srgb = settings.colorSpace == ImportColorSpace::Srgb;
    }
    effective.generateMips = requested.generateMips && settings.generateMips;
    if (requested.compression != TextureCompression::None)
    {
        effective.compression =
            detail::sidecarCompression(settings.compression).value_or(requested.compression);
    }
    return effective;
}

std::optional<TextureCompression> detail::sidecarCompression(ImportCompression compression) noexcept
{
    switch (compression)
    {
    case ImportCompression::Auto:
        break;
    case ImportCompression::None:
        return TextureCompression::None;
    case ImportCompression::BC7:
        return TextureCompression::BC7;
    case ImportCompression::BC5:
        return TextureCompression::BC5;
    case ImportCompression::BC4:
        return TextureCompression::BC4;
    case ImportCompression::BC6H:
        return TextureCompression::BC6H;
    }
    return std::nullopt;
}

} // namespace illusion
