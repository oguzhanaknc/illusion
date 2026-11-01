#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/ImportSettings.hpp>
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

constexpr std::array<std::string_view, 3> kColorSpaceNames{"auto", "srgb", "linear"};

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
            const std::optional<std::string_view> name = value.asString();
            const auto found = name ? std::find(kColorSpaceNames.begin(), kColorSpaceNames.end(), *name)
                                    : kColorSpaceNames.end();
            if (found == kColorSpaceNames.end())
            {
                return unexpected(fail(pointer, "must be \"auto\", \"srgb\" or \"linear\""));
            }
            merged.colorSpace = static_cast<ImportColorSpace>(found - kColorSpaceNames.begin());
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
        else
        {
            return unexpected(fail(pointer, "is not a texture setting (colorSpace, generateMips, maxSize)"));
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
    for (const JsonMember member : patch.members())
    {
        const std::string pointer = std::format("/model/{}", member.key);
        if (member.key != "scale")
        {
            return unexpected(fail(pointer, "is not a model setting (scale)"));
        }
        if (member.value.isNull())
        {
            merged.scale = ModelImportSettings{}.scale;
            continue;
        }
        const std::optional<f64> scale = member.value.asF64();
        if (!scale || !(*scale > 0.0) || *scale > kMaxModelScale)
        {
            return unexpected(fail(pointer, "must be a number greater than 0 and at most 1000"));
        }
        merged.scale = static_cast<f32>(*scale);
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
        any = true;
    }
    if (kind == ImportSettingsKind::Model && settings.model != defaultModel)
    {
        JsonMutValue model = root.addObject("model");
        model.add("scale", document.makeF32(settings.model.scale));
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
        return std::format("{{\"texture\":{{\"colorSpace\":\"{}\",\"generateMips\":{},\"maxSize\":{}}}}}",
                           kColorSpaceNames[static_cast<usize>(settings.texture.colorSpace)],
                           settings.texture.generateMips, settings.texture.maxSize);
    }
    if (kind == ImportSettingsKind::Model)
    {
        JsonMutDocument document;
        JsonMutValue root = document.makeObject();
        root.addObject("model").add("scale", document.makeF32(settings.model.scale));
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
    return effective;
}

} // namespace illusion
