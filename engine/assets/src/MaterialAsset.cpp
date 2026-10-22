#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/StructRegistry.hpp>

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

} // namespace

std::array<MaterialTextureUse, 5> materialTextureUses(const MaterialTextures& textures) noexcept
{
    return {MaterialTextureUse{textures.baseColor, true},
            MaterialTextureUse{textures.metallicRoughness, false}, MaterialTextureUse{textures.normal, false},
            MaterialTextureUse{textures.occlusion, false}, MaterialTextureUse{textures.emissive, true}};
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
