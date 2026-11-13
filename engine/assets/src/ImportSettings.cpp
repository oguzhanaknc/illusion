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
    return kind == ImportSettingsKind::Texture ? "texture" : "model";
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

// Merges the section @p member ("texture" or "model") into @p settings; only the section of @p kind exists.
Expected<void, ImportSettingsError> mergeSection(ImportSettings& settings, const JsonMember& member,
                                                 ImportSettingsKind kind)
{
    if (member.key != sectionName(kind))
    {
        return unexpected(
            fail(std::format("/{}", member.key),
                 member.key == "texture" || member.key == "model"
                     ? std::format("does not apply to this asset (it has \"{}\" settings)", sectionName(kind))
                     : std::format("is not a settings section (\"{}\")", sectionName(kind))));
    }
    return kind == ImportSettingsKind::Texture ? mergeTexture(settings.texture, member.value)
                                               : mergeModel(settings.model, member.value);
}

} // namespace

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
        return unexpected(fail("", "this kind of asset has no import settings (only textures and models)"));
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
        return unexpected(fail("", "this kind of asset has no import settings (only textures and models)"));
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
        switch (settings.compression)
        {
        case ImportCompression::Auto:
            break;
        case ImportCompression::None:
            effective.compression = TextureCompression::None;
            break;
        case ImportCompression::BC7:
            effective.compression = TextureCompression::BC7;
            break;
        case ImportCompression::BC5:
            effective.compression = TextureCompression::BC5;
            break;
        case ImportCompression::BC4:
            effective.compression = TextureCompression::BC4;
            break;
        case ImportCompression::BC6H:
            effective.compression = TextureCompression::BC6H;
            break;
        }
    }
    return effective;
}

} // namespace illusion
