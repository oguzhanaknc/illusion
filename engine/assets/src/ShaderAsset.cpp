#include <illusion/assets/ShaderAsset.hpp>
#include <illusion/core/Serialization.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace illusion
{
namespace
{

constexpr u32 kMagic = 0x44485349; // "ISHD"
// Version 2 added the decal channels (ADR 0066); version 1 shaders read as surface shaders.
constexpr u32 kVersion = 2;
constexpr u32 kSurfaceOnlyVersion = 1;
constexpr u32 kMaxNameBytes = 256;

constexpr std::array<std::string_view, 5> kShadingModelNames = {"Default", "Unlit", "Subsurface",
                                                                "TwoSidedFoliage", "ClearCoat"};

// In flag order: bit i is kDecalChannelNames[i].
constexpr std::array<std::string_view, 4> kDecalChannelNames = {"BaseColor", "Normal", "ORM", "Emission"};

template <class Enum>
bool readEnum(BinaryReader& reader, Enum& out, u8 count) noexcept
{
    u8 value = 0;
    if (!reader.read(value) || value >= count)
    {
        reader.fail();
        return false;
    }
    out = static_cast<Enum>(value);
    return true;
}

bool readName(BinaryReader& reader, std::string& out)
{
    if (!reader.readString(out) || out.empty() || out.size() > kMaxNameBytes)
    {
        reader.fail();
        return false;
    }
    return true;
}

u32 variantOrder(const ShaderVariant& variant) noexcept
{
    return static_cast<u32>(variant.pass) * 3u + static_cast<u32>(variant.alphaMode);
}

template <class T>
void writeValue(std::vector<std::byte>& block, u32 offset, const T& value) noexcept
{
    std::memcpy(block.data() + offset, &value, sizeof(T));
}

} // namespace

std::string_view shadingModelName(ShadingModel model) noexcept
{
    return kShadingModelNames[static_cast<usize>(model)];
}

std::optional<ShadingModel> parseShadingModel(std::string_view name) noexcept
{
    for (usize i = 0; i < kShadingModelNames.size(); ++i)
    {
        if (kShadingModelNames[i] == name)
        {
            return static_cast<ShadingModel>(i);
        }
    }
    return std::nullopt;
}

std::string decalChannelsName(DecalChannels channels)
{
    std::string names;
    for (usize i = 0; i < kDecalChannelNames.size(); ++i)
    {
        if (hasDecalChannel(channels, static_cast<DecalChannels>(1u << i)))
        {
            names += names.empty() ? "" : " ";
            names += kDecalChannelNames[i];
        }
    }
    return names;
}

std::optional<DecalChannels> parseDecalChannels(std::string_view names) noexcept
{
    DecalChannels channels = DecalChannels::None;
    usize at = 0;
    while (at < names.size())
    {
        if (names[at] == ' ')
        {
            ++at;
            continue;
        }
        const usize end = std::min(names.find(' ', at), names.size());
        const std::string_view name = names.substr(at, end - at);
        at = end;
        const auto found = std::find(kDecalChannelNames.begin(), kDecalChannelNames.end(), name);
        if (found == kDecalChannelNames.end())
        {
            return std::nullopt;
        }
        const auto channel =
            static_cast<DecalChannels>(1u << static_cast<u32>(found - kDecalChannelNames.begin()));
        if (hasDecalChannel(channels, channel))
        {
            return std::nullopt;
        }
        channels = channels | channel;
    }
    if (channels == DecalChannels::None)
    {
        return std::nullopt;
    }
    return channels;
}

std::string_view shaderPassName(ShaderPass pass) noexcept
{
    constexpr std::array<std::string_view, kShaderPassCount> kNames = {
        "forward",       "forwardBindless", "depthPrepass", "shadowCaster",
        "motionVectors", "entityIds",       "decal",        "decalEmission"};
    return kNames[static_cast<usize>(pass)];
}

std::string_view alphaModeName(AlphaMode mode) noexcept
{
    constexpr std::array<std::string_view, 3> kNames = {"opaque", "mask", "blend"};
    return kNames[static_cast<usize>(mode)];
}

std::string_view shaderParameterTypeName(ShaderParameterType type) noexcept
{
    constexpr std::array<std::string_view, 6> kNames = {"float", "float2", "float3", "float4", "int", "bool"};
    return kNames[static_cast<usize>(type)];
}

u32 shaderParameterComponents(ShaderParameterType type) noexcept
{
    switch (type)
    {
    case ShaderParameterType::Float2:
        return 2;
    case ShaderParameterType::Float3:
        return 3;
    case ShaderParameterType::Float4:
        return 4;
    default:
        return 1;
    }
}

std::string_view shaderTextureUsageName(ShaderTextureUsage usage) noexcept
{
    constexpr std::array<std::string_view, 4> kNames = {"color", "linear", "normal", "mask"};
    return kNames[static_cast<usize>(usage)];
}

const ShaderVariant* ShaderAsset::variant(ShaderPass pass, AlphaMode mode) const noexcept
{
    const auto found = std::find_if(variants.begin(), variants.end(), [&](const ShaderVariant& candidate) {
        return candidate.pass == pass && candidate.alphaMode == mode;
    });
    return found == variants.end() ? nullptr : &*found;
}

const ShaderParameter* ShaderAsset::findParameter(std::string_view name) const noexcept
{
    const auto found = std::find_if(parameters.begin(), parameters.end(),
                                    [&](const ShaderParameter& parameter) { return parameter.name == name; });
    return found == parameters.end() ? nullptr : &*found;
}

const ShaderTexture* ShaderAsset::findTexture(std::string_view name) const noexcept
{
    const auto found = std::find_if(textures.begin(), textures.end(),
                                    [&](const ShaderTexture& texture) { return texture.name == name; });
    return found == textures.end() ? nullptr : &*found;
}

std::vector<std::byte> defaultParameterBlock(const ShaderAsset& shader)
{
    std::vector<std::byte> block(shader.parameterBytes);
    for (const ShaderParameter& parameter : shader.parameters)
    {
        switch (parameter.type)
        {
        case ShaderParameterType::Int:
            writeValue(block, parameter.offset, static_cast<i32>(std::lround(parameter.defaultValue[0])));
            break;
        case ShaderParameterType::Bool:
            writeValue(block, parameter.offset, parameter.defaultValue[0] != 0.0f ? 1u : 0u);
            break;
        default:
            std::memcpy(block.data() + parameter.offset, parameter.defaultValue.data(),
                        shaderParameterComponents(parameter.type) * sizeof(f32));
            break;
        }
    }
    for (const ShaderTexture& texture : shader.textures)
    {
        writeValue(block, texture.offset, texture.slot);
    }
    return block;
}

std::vector<std::byte> serializeShader(const ShaderAsset& shader)
{
    std::vector<std::byte> bytes;
    usize spirvBytes = 0;
    for (const ShaderVariant& variant : shader.variants)
    {
        spirvBytes += variant.spirv.size() + 16;
    }
    bytes.reserve(256 + shader.source.size() + shader.parameters.size() * 64 + shader.textures.size() * 64 +
                  spirvBytes);
    BinaryWriter writer(bytes);
    writer.write(kMagic);
    writer.write(kVersion);
    writer.writeString(shader.source);
    writer.write(static_cast<u8>(shader.shadingModel));
    writer.write(static_cast<u8>(shader.decalChannels));
    writer.write(static_cast<u8>(shader.worldOffset ? 1 : 0));
    writer.write(shader.parameterBytes);
    writer.write(shader.libraryHash);
    writer.writeString(shader.compilerVersion);
    writer.write(static_cast<u32>(shader.parameters.size()));
    for (const ShaderParameter& parameter : shader.parameters)
    {
        writer.writeString(parameter.name);
        writer.write(static_cast<u8>(parameter.type));
        writer.write(parameter.offset);
        writer.write(parameter.defaultValue);
        writer.write(static_cast<u8>(parameter.color ? 1 : 0));
        writer.write(static_cast<u8>(parameter.hasRange ? 1 : 0));
        writer.write(parameter.minimum);
        writer.write(parameter.maximum);
    }
    writer.write(static_cast<u32>(shader.textures.size()));
    for (const ShaderTexture& texture : shader.textures)
    {
        writer.writeString(texture.name);
        writer.write(texture.offset);
        writer.write(texture.slot);
        writer.write(static_cast<u8>(texture.usage));
        writer.write(static_cast<u8>(texture.fallback));
        writer.writeString(texture.defaultTexture);
    }
    writer.write(static_cast<u32>(shader.variants.size()));
    for (const ShaderVariant& variant : shader.variants)
    {
        writer.write(static_cast<u8>(variant.pass));
        writer.write(static_cast<u8>(variant.alphaMode));
        writer.writeVector(std::span<const std::byte>(variant.spirv));
    }
    return bytes;
}

std::optional<ShaderAsset> deserializeShader(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    u32 magic = 0;
    u32 version = 0;
    if (!reader.read(magic) || magic != kMagic || !reader.read(version) ||
        (version != kVersion && version != kSurfaceOnlyVersion))
    {
        return std::nullopt;
    }
    ShaderAsset shader;
    u8 decalChannels = 0;
    u8 worldOffset = 0;
    reader.readString(shader.source);
    readEnum(reader, shader.shadingModel, static_cast<u8>(kShadingModelNames.size()));
    if (version != kSurfaceOnlyVersion)
    {
        reader.read(decalChannels);
    }
    reader.read(worldOffset);
    reader.read(shader.parameterBytes);
    reader.read(shader.libraryHash);
    reader.readString(shader.compilerVersion);
    u32 parameterCount = 0;
    reader.read(parameterCount);
    const bool decal = decalChannels != 0;
    if (!reader.ok() || worldOffset > 1 || decalChannels > static_cast<u8>(kAllDecalChannels) ||
        (decal && (worldOffset != 0 || shader.shadingModel != ShadingModel::Default)) ||
        shader.parameterBytes > kMaxShaderParameterBytes || parameterCount > kMaxShaderParameterBytes / 4)
    {
        return std::nullopt;
    }
    shader.decalChannels = static_cast<DecalChannels>(decalChannels);
    shader.worldOffset = worldOffset != 0;
    shader.parameters.resize(parameterCount);
    for (ShaderParameter& parameter : shader.parameters)
    {
        u8 color = 0;
        u8 hasRange = 0;
        readName(reader, parameter.name);
        readEnum(reader, parameter.type, 6);
        reader.read(parameter.offset);
        reader.read(parameter.defaultValue);
        reader.read(color);
        reader.read(hasRange);
        reader.read(parameter.minimum);
        reader.read(parameter.maximum);
        const bool finite = std::all_of(parameter.defaultValue.begin(), parameter.defaultValue.end(),
                                        [](f32 value) { return std::isfinite(value); }) &&
                            std::isfinite(parameter.minimum) && std::isfinite(parameter.maximum);
        if (!reader.ok() || color > 1 || hasRange > 1 || !finite || parameter.offset % 4 != 0 ||
            parameter.offset + shaderParameterComponents(parameter.type) * 4 > shader.parameterBytes)
        {
            return std::nullopt;
        }
        parameter.color = color != 0;
        parameter.hasRange = hasRange != 0;
    }
    u32 textureCount = 0;
    if (!reader.read(textureCount) || textureCount > kMaxShaderTextures)
    {
        return std::nullopt;
    }
    shader.textures.resize(textureCount);
    for (u32 slot = 0; slot < textureCount; ++slot)
    {
        ShaderTexture& texture = shader.textures[slot];
        readName(reader, texture.name);
        reader.read(texture.offset);
        reader.read(texture.slot);
        readEnum(reader, texture.usage, 4);
        readEnum(reader, texture.fallback, 3);
        reader.readString(texture.defaultTexture);
        if (!reader.ok() || texture.slot != slot || texture.offset % 4 != 0 ||
            texture.offset + 4 > shader.parameterBytes)
        {
            return std::nullopt;
        }
    }
    u32 variantCount = 0;
    if (!reader.read(variantCount) || variantCount > kShaderPassCount * 3)
    {
        return std::nullopt;
    }
    shader.variants.resize(variantCount);
    for (usize i = 0; i < shader.variants.size(); ++i)
    {
        ShaderVariant& variant = shader.variants[i];
        readEnum(reader, variant.pass, static_cast<u8>(kShaderPassCount));
        readEnum(reader, variant.alphaMode, 3);
        reader.readVector(variant.spirv);
        if (!reader.ok() || variant.spirv.empty() || variant.spirv.size() % 4 != 0 ||
            !shaderVariantExists(variant.pass, variant.alphaMode, shader.decalChannels) ||
            (i > 0 && variantOrder(variant) <= variantOrder(shader.variants[i - 1])))
        {
            return std::nullopt;
        }
    }
    if (reader.remaining() != 0)
    {
        return std::nullopt;
    }
    return shader;
}

} // namespace illusion
