#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <cmath>
#include <cstdint>
#include <format>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.material";

template <class T>
const StructReflection* reflectionOf()
{
    return ComponentRegistry::instance().structs().reflectionOf(typeHash<T>());
}

f32 srgbToLinear(u32 value) noexcept
{
    const f32 c = static_cast<f32>(value) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// "#rrggbb" or "#rrggbbaa" as linear components; alpha is not a colour and stays as written.
std::optional<std::array<f32, 4>> parseHexColor(std::string_view text) noexcept
{
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
    {
        return std::nullopt;
    }
    std::array<f32, 4> out{0.0f, 0.0f, 0.0f, 1.0f};
    for (usize channel = 0; 1 + channel * 2 < text.size(); ++channel)
    {
        u32 value = 0;
        for (usize digit = 0; digit < 2; ++digit)
        {
            const char c = text[1 + channel * 2 + digit];
            u32 nibble = 0;
            if (c >= '0' && c <= '9')
            {
                nibble = static_cast<u32>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                nibble = static_cast<u32>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                nibble = static_cast<u32>(c - 'A' + 10);
            }
            else
            {
                return std::nullopt;
            }
            value = value * 16 + nibble;
        }
        out[channel] = channel == 3 ? static_cast<f32>(value) / 255.0f : srgbToLinear(value);
    }
    return out;
}

std::string declaredNames(const ShaderAsset& shader)
{
    std::string names;
    const auto add = [&names](const std::string& name) {
        names += names.empty() ? "" : ", ";
        names += name;
    };
    for (const ShaderParameter& parameter : shader.parameters)
    {
        add(parameter.name);
    }
    for (const ShaderTexture& texture : shader.textures)
    {
        add(texture.name);
    }
    return names.empty() ? std::string("none") : names;
}

// "/parameters/<name>" with the escapes of RFC 6901.
std::string pointerOf(std::string_view name)
{
    std::string pointer = "/parameters/";
    for (const char c : name)
    {
        if (c == '~')
        {
            pointer += "~0";
        }
        else if (c == '/')
        {
            pointer += "~1";
        }
        else
        {
            pointer += c;
        }
    }
    return pointer;
}

// A problem with a value: a pointer relative to the parameter ("" or "/1") and what is wrong.
struct ParameterProblem
{
    std::string pointer;
    std::string message;
};

Expected<std::array<f32, 4>, ParameterProblem> readParameter(const ShaderParameter& parameter, JsonValue json)
{
    std::array<f32, 4> value = parameter.defaultValue;
    const u32 components = shaderParameterComponents(parameter.type);
    switch (parameter.type)
    {
    case ShaderParameterType::Float:
    {
        const std::optional<f64> number = json.asF64();
        if (!number)
        {
            return unexpected(ParameterProblem{"", "must be a number"});
        }
        value[0] = static_cast<f32>(*number);
        return value;
    }
    case ShaderParameterType::Int:
    {
        const std::optional<i64> number = json.asI64();
        if (!number || *number < INT32_MIN || *number > INT32_MAX)
        {
            return unexpected(ParameterProblem{"", "must be an integer"});
        }
        value[0] = static_cast<f32>(*number);
        return value;
    }
    case ShaderParameterType::Bool:
    {
        const std::optional<bool> flag = json.asBool();
        if (!flag)
        {
            return unexpected(ParameterProblem{"", "must be true or false"});
        }
        value[0] = *flag ? 1.0f : 0.0f;
        return value;
    }
    case ShaderParameterType::Float2:
    case ShaderParameterType::Float3:
    case ShaderParameterType::Float4:
        break;
    }
    if (parameter.color)
    {
        if (const std::optional<std::string_view> text = json.asString())
        {
            const std::optional<std::array<f32, 4>> color = parseHexColor(*text);
            if (!color || (components == 3 && text->size() != 7))
            {
                return unexpected(ParameterProblem{
                    "", std::format("must be \"#rrggbb\"{} or an array of {} numbers",
                                    components == 4 ? " or \"#rrggbbaa\"" : "", components)});
            }
            for (u32 i = 0; i < components; ++i)
            {
                value[i] = (*color)[i];
            }
            return value;
        }
    }
    if (!json.isArray() || json.size() != components)
    {
        return unexpected(ParameterProblem{"", std::format("must be an array of {} numbers", components)});
    }
    u32 index = 0;
    for (const JsonValue element : json.elements())
    {
        const std::optional<f64> number = element.asF64();
        if (!number)
        {
            return unexpected(ParameterProblem{std::format("/{}", index), "must be a number"});
        }
        value[index++] = static_cast<f32>(*number);
    }
    return value;
}

} // namespace

MaterialTextureUse shaderTextureUse(const ShaderTexture& texture, const AssetRef& image) noexcept
{
    switch (texture.usage)
    {
    case ShaderTextureUsage::Color:
        return {image, true, TextureCompression::BC7};
    case ShaderTextureUsage::Linear:
        return {image, false, TextureCompression::BC7};
    case ShaderTextureUsage::Normal:
        return {image, false, TextureCompression::BC5};
    case ShaderTextureUsage::Mask:
        return {image, false, TextureCompression::BC4};
    }
    return {image, false, TextureCompression::BC7};
}

Expected<MaterialParameters, std::string> resolveMaterialParameters(const MaterialAsset& material,
                                                                    const ShaderAsset& shader)
{
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(material.parameters.text);
    if (!doc)
    {
        return unexpected(std::string("/parameters: is not valid JSON"));
    }
    const JsonValue root = doc->root();
    if (!root.isObject())
    {
        return unexpected(std::string("/parameters: must be an object"));
    }
    for (const JsonMember member : root.members())
    {
        if (shader.findParameter(member.key) == nullptr && shader.findTexture(member.key) == nullptr)
        {
            return unexpected(std::format("{}: the shader '{}' declares no parameter '{}' (it declares: {})",
                                          pointerOf(member.key), shader.source, member.key,
                                          declaredNames(shader)));
        }
    }
    MaterialParameters resolved;
    resolved.values.reserve(shader.parameters.size());
    for (const ShaderParameter& parameter : shader.parameters)
    {
        MaterialParameterValue value{parameter.name, parameter.defaultValue};
        if (const JsonValue json = root.find(parameter.name); !json.isAbsent())
        {
            Expected<std::array<f32, 4>, ParameterProblem> read = readParameter(parameter, json);
            if (!read)
            {
                return unexpected(std::format("{}{}: {}", pointerOf(parameter.name), read.error().pointer,
                                              read.error().message));
            }
            value.value = *read;
            value.set = true;
        }
        resolved.values.push_back(std::move(value));
    }
    resolved.textures.reserve(shader.textures.size());
    for (const ShaderTexture& texture : shader.textures)
    {
        AssetRef image = texture.defaultTexture.empty() ? AssetRef{} : AssetRef(texture.defaultTexture);
        bool set = false;
        if (const JsonValue json = root.find(texture.name); !json.isAbsent())
        {
            set = true;
            const std::optional<std::string_view> path = json.asString();
            if (!path)
            {
                return unexpected(
                    std::format("{}: must be an asset path (\"\" for no image)", pointerOf(texture.name)));
            }
            image = path->empty() ? AssetRef{} : AssetRef(*path);
            if (!path->empty() && image.empty())
            {
                return unexpected(
                    std::format("{}: '{}' is not a valid asset path", pointerOf(texture.name), *path));
            }
        }
        resolved.textures.push_back(
            MaterialTextureParameter{texture.name, image, shaderTextureUse(texture, image), set});
    }
    return resolved;
}

std::array<MaterialTextureUse, 5> materialTextureUses(const MaterialTextures& textures) noexcept
{
    const TextureCompression occlusion =
        !textures.occlusion.empty() && textures.occlusion == textures.metallicRoughness
            ? TextureCompression::BC7
            : TextureCompression::BC4;
    return {MaterialTextureUse{textures.baseColor, true, TextureCompression::BC7},
            MaterialTextureUse{textures.metallicRoughness, false, TextureCompression::BC7},
            MaterialTextureUse{textures.normal, false, TextureCompression::BC5},
            MaterialTextureUse{textures.occlusion, false, occlusion},
            MaterialTextureUse{textures.emissive, true, TextureCompression::BC7}};
}

Expected<MaterialAsset, std::string> parseMaterialAsset(std::string_view json)
{
    const StructReflection* material = reflectionOf<MaterialData>();
    const StructReflection* textures = reflectionOf<MaterialTextures>();
    if (material == nullptr || textures == nullptr)
    {
        return unexpected(std::string("material types are not reflected (call reflectAssetStructs())"));
    }
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json);
    if (!doc)
    {
        return unexpected(std::format("invalid JSON at line {}, column {}: {}", doc.error().line,
                                      doc.error().column, doc.error().message));
    }
    const JsonValue root = doc->root();
    if (!root.isObject() || root.find("format").asString() != kFormatName)
    {
        return unexpected(std::string("not a material file (missing \"format\": \"illusion.material\")"));
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > kMaterialAssetVersion)
    {
        return unexpected(
            std::format("unsupported material version (this build reads 1 to {})", kMaterialAssetVersion));
    }
    MaterialAsset asset;
    const auto readMember = [&](std::string_view key, const StructReflection& type,
                                void* object) -> std::optional<std::string> {
        const JsonValue value = root.find(key);
        if (value.isAbsent())
        {
            return std::nullopt;
        }
        const ValueReadResult read = readValue(type, object, value, {.mode = JsonReadMode::Merge});
        if (!read.ok())
        {
            return std::format("/{}{}: {}", key, read.errors.front().pointer, read.errors.front().message);
        }
        return std::nullopt;
    };
    if (std::optional<std::string> error = readMember("material", *material, &asset.material))
    {
        return unexpected(std::move(*error));
    }
    if (std::optional<std::string> error = readMember("textures", *textures, &asset.textures))
    {
        return unexpected(std::move(*error));
    }
    // Version 1 files have neither member: they read as materials of the standard shader.
    if (const JsonValue shader = root.find("shader"); !shader.isAbsent())
    {
        const std::optional<std::string_view> path = shader.asString();
        if (!path)
        {
            return unexpected(std::string("/shader: must be the path of a .shader file (\"\" for none)"));
        }
        asset.shader = path->empty() ? AssetRef{} : AssetRef(*path);
        if (!path->empty() && asset.shader.empty())
        {
            return unexpected(std::format("/shader: '{}' is not a valid asset path", *path));
        }
    }
    if (const JsonValue parameters = root.find("parameters"); !parameters.isAbsent())
    {
        if (!parameters.isObject())
        {
            return unexpected(std::string("/parameters: must be an object"));
        }
        if (!parameters.write(asset.parameters.text, JsonWriteOptions{.pretty = false}))
        {
            return unexpected(std::string("/parameters: cannot be read"));
        }
    }
    return asset;
}

Expected<std::string, std::string> writeMaterialAsset(const MaterialAsset& asset)
{
    const StructReflection* material = reflectionOf<MaterialData>();
    const StructReflection* textures = reflectionOf<MaterialTextures>();
    if (material == nullptr || textures == nullptr)
    {
        return unexpected(std::string("material types are not reflected (call reflectAssetStructs())"));
    }
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kFormatName));
    root.add("version", doc.makeU64(kMaterialAssetVersion));
    if (!asset.shader.empty())
    {
        if (asset.shader.path().empty())
        {
            return unexpected(std::string("/shader: the asset has no recorded path"));
        }
        root.add("shader", doc.makeString(asset.shader.path()));
    }
    const auto addMember = [&](std::string_view key, const StructReflection& type,
                               const void* object) -> std::optional<std::string> {
        Expected<JsonMutValue, ValueError> value = writeValue(doc, type, object);
        if (!value)
        {
            return std::format("/{}{}: {}", key, value.error().pointer, value.error().message);
        }
        root.add(key, *value);
        return std::nullopt;
    };
    if (std::optional<std::string> error = addMember("material", *material, &asset.material))
    {
        return unexpected(std::move(*error));
    }
    if (std::optional<std::string> error = addMember("textures", *textures, &asset.textures))
    {
        return unexpected(std::move(*error));
    }
    Expected<JsonDocument, JsonError> parameters = JsonDocument::parse(asset.parameters.text);
    if (!parameters || !parameters->root().isObject())
    {
        return unexpected(std::string("/parameters: must be a JSON object"));
    }
    if (parameters->root().size() != 0)
    {
        root.add("parameters", doc.copy(parameters->root()));
    }
    doc.setRoot(root);
    std::string text;
    if (Expected<void, JsonError> written = doc.write(text); !written)
    {
        return unexpected(std::move(written.error().message));
    }
    return text;
}

AssetFileDecoder<MaterialAsset> materialAssetDecoder()
{
    return [](std::vector<std::byte>&& bytes, std::string& error) -> std::optional<MaterialAsset> {
        Expected<MaterialAsset, std::string> asset =
            parseMaterialAsset(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (!asset)
        {
            error = std::move(asset.error());
            return std::nullopt;
        }
        return std::move(*asset);
    };
}

} // namespace illusion
