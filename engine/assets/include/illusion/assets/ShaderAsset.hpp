#pragma once

/// @file
/// Cooked material shaders (ADR 0063): the parameter layout that the compiler's reflection gave a `.shader`
/// file, its shading model or decal channels (ADR 0066), and SPIR-V for every pass and alpha mode it is drawn
/// with. ShaderCompiler.hpp makes them; the player only loads them (AssetType::Shader).

#include <illusion/assets/Model.hpp>
#include <illusion/core/Base.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// File extension of material shader sources.
inline constexpr std::string_view kShaderAssetExtension = ".shader";

/// Largest parameter block of a material shader, in bytes.
inline constexpr u32 kMaxShaderParameterBytes = 256;

/// Most textures a material shader declares.
inline constexpr u32 kMaxShaderTextures = 8;

/// Name of the vertex entry point in every variant's SPIR-V.
inline constexpr std::string_view kShaderVertexEntry = "vertexMain";

/// Name of the fragment entry point in every variant's SPIR-V.
inline constexpr std::string_view kShaderFragmentEntry = "fragmentMain";

/// How the lighting code shades a material (`[ShadingModel("...")]` on its Parameters).
enum class ShadingModel : u8
{
    Default,         ///< Metallic-roughness PBR.
    Unlit,           ///< Base colour plus emission, no lighting.
    Subsurface,      ///< Wrap lighting with a scatter colour and thickness.
    TwoSidedFoliage, ///< Leaf translucency: light through thin surfaces.
    ClearCoat,       ///< A second specular lobe over the base.
};

/// Name of @p model as written in shaders ("Default", "Unlit", ...).
[[nodiscard]] std::string_view shadingModelName(ShadingModel model) noexcept;

/// The shading model named @p name, if any.
[[nodiscard]] std::optional<ShadingModel> parseShadingModel(std::string_view name) noexcept;

/// The channels of the decal buffer a decal shader writes (`[Decal("...")]` on its Parameters, ADR 0066); a
/// set of flags, None for surface shaders.
enum class DecalChannels : u8
{
    None = 0,      ///< Not a decal shader.
    BaseColor = 1, ///< Base colour.
    Normal = 2,    ///< Normal.
    ORM = 4,       ///< Occlusion, roughness and metallic together.
    Emission = 8,  ///< Emission, added to the HDR colour.
};

/// Every decal channel.
inline constexpr DecalChannels kAllDecalChannels = static_cast<DecalChannels>(15);

/// The union of @p a and @p b.
[[nodiscard]] constexpr DecalChannels operator|(DecalChannels a, DecalChannels b) noexcept
{
    return static_cast<DecalChannels>(static_cast<u32>(a) | static_cast<u32>(b));
}

/// The channels in both @p a and @p b.
[[nodiscard]] constexpr DecalChannels operator&(DecalChannels a, DecalChannels b) noexcept
{
    return static_cast<DecalChannels>(static_cast<u32>(a) & static_cast<u32>(b));
}

/// True if @p set holds any channel of @p channels.
[[nodiscard]] constexpr bool hasDecalChannel(DecalChannels set, DecalChannels channels) noexcept
{
    return (set & channels) != DecalChannels::None;
}

/// The channels of @p channels as written in shaders, separated by spaces in flag order ("BaseColor ORM";
/// "" for none).
[[nodiscard]] std::string decalChannelsName(DecalChannels channels);

/// The channels of a space-separated list such as "BaseColor Normal"; std::nullopt for an empty list, an
/// unknown name or a channel listed twice.
[[nodiscard]] std::optional<DecalChannels> parseDecalChannels(std::string_view names) noexcept;

/// A pass that draws material shaders; each has a variant per alpha mode.
enum class ShaderPass : u8
{
    Forward,         ///< Lit colour, bound material resources.
    ForwardBindless, ///< Lit colour, parameters and textures indexed by the instance's material.
    DepthPrepass,    ///< Depth only.
    ShadowCaster,    ///< Depth into a shadow view.
    MotionVectors,   ///< Depth and motion vectors.
    EntityIds,       ///< Motion vectors and Entity ids (only Entity ids for blended materials).
    Decal,           ///< A decal box into the decal buffer (decal shaders only).
    DecalEmission,   ///< A decal box adding its emission to the HDR colour (decal shaders only).
};

/// Number of ShaderPass values.
inline constexpr u32 kShaderPassCount = 8;

/// Stable lower-case name of @p pass, e.g. "forwardBindless".
[[nodiscard]] std::string_view shaderPassName(ShaderPass pass) noexcept;

/// Stable lower-case name of @p mode: "opaque", "mask" or "blend".
[[nodiscard]] std::string_view alphaModeName(AlphaMode mode) noexcept;

/// True if surface shaders with @p mode are drawn in @p pass: never in the decal passes, and blended ones
/// only in the forward passes and for Entity ids.
[[nodiscard]] constexpr bool shaderVariantExists(ShaderPass pass, AlphaMode mode) noexcept
{
    if (pass == ShaderPass::Decal || pass == ShaderPass::DecalEmission)
    {
        return false;
    }
    return mode != AlphaMode::Blend || pass == ShaderPass::Forward || pass == ShaderPass::ForwardBindless ||
           pass == ShaderPass::EntityIds;
}

/// True if a shader with the decal channels @p decal (None: a surface shader) has a variant for @p pass and
/// @p mode. Decal shaders are keyed as blended and drawn only in ShaderPass::Decal (with a channel other than
/// Emission) and ShaderPass::DecalEmission (with Emission).
[[nodiscard]] constexpr bool shaderVariantExists(ShaderPass pass, AlphaMode mode,
                                                 DecalChannels decal) noexcept
{
    if (decal == DecalChannels::None)
    {
        return shaderVariantExists(pass, mode);
    }
    if (mode != AlphaMode::Blend)
    {
        return false;
    }
    if (pass == ShaderPass::Decal)
    {
        return hasDecalChannel(decal, DecalChannels::BaseColor | DecalChannels::Normal | DecalChannels::ORM);
    }
    return pass == ShaderPass::DecalEmission && hasDecalChannel(decal, DecalChannels::Emission);
}

/// The type of a material parameter.
enum class ShaderParameterType : u8
{
    Float,  ///< `float`.
    Float2, ///< `float2`.
    Float3, ///< `float3` (a colour with `[Color]`).
    Float4, ///< `float4` (a colour with `[Color]`).
    Int,    ///< `int`.
    Bool,   ///< `bool` (4 bytes: 0 or 1).
};

/// Stable lower-case name of @p type, e.g. "float3".
[[nodiscard]] std::string_view shaderParameterTypeName(ShaderParameterType type) noexcept;

/// Number of components of @p type (1 to 4).
[[nodiscard]] u32 shaderParameterComponents(ShaderParameterType type) noexcept;

/// A parameter of a material shader.
struct ShaderParameter
{
    std::string name{};                                      ///< Field name in Parameters.
    ShaderParameterType type = ShaderParameterType::Float;   ///< Type.
    u32 offset = 0;                                          ///< Byte offset in the parameter block.
    std::array<f32, 4> defaultValue{0.0f, 0.0f, 0.0f, 0.0f}; ///< Default (ints and bools as numbers).
    bool color = false;                                      ///< A colour (`[Color]`).
    bool hasRange = false;                                   ///< minimum and maximum are set (`[Range]`).
    f32 minimum = 0.0f;                                      ///< Lowest value the editor offers.
    f32 maximum = 0.0f;                                      ///< Highest value the editor offers.

    /// Parameters compare by every field.
    friend bool operator==(const ShaderParameter&, const ShaderParameter&) = default;
};

/// How the image of a texture parameter is imported.
enum class ShaderTextureUsage : u8
{
    Color,  ///< sRGB colour (BC7).
    Linear, ///< Linear data (BC7).
    Normal, ///< Tangent-space normal map (BC5).
    Mask,   ///< One channel (BC4).
};

/// Stable lower-case name of @p usage: "color", "linear", "normal" or "mask".
[[nodiscard]] std::string_view shaderTextureUsageName(ShaderTextureUsage usage) noexcept;

/// What a texture parameter samples when its material sets no image.
enum class ShaderTextureFallback : u8
{
    White,      ///< (1, 1, 1, 1).
    Black,      ///< (0, 0, 0, 1).
    FlatNormal, ///< (0.5, 0.5, 1, 1): the geometric normal.
};

/// A texture parameter of a material shader.
struct ShaderTexture
{
    std::string name{};                                   ///< Field name in Parameters.
    u32 offset = 0;                                       ///< Byte offset of its Texture2DSlot.
    u32 slot = 0;                                         ///< Index among the textures, in field order.
    ShaderTextureUsage usage = ShaderTextureUsage::Color; ///< Import settings of its images.
    ShaderTextureFallback fallback = ShaderTextureFallback::White; ///< Used without an image.
    std::string defaultTexture{}; ///< Asset path of the default image ("" for the fallback).

    /// Textures compare by every field.
    friend bool operator==(const ShaderTexture&, const ShaderTexture&) = default;
};

/// The SPIR-V of one pass and alpha mode: one module with the entry points kShaderVertexEntry and
/// kShaderFragmentEntry.
struct ShaderVariant
{
    ShaderPass pass = ShaderPass::Forward;   ///< Pass.
    AlphaMode alphaMode = AlphaMode::Opaque; ///< Alpha mode.
    std::vector<std::byte> spirv{};          ///< SPIR-V words (little-endian).

    /// Variants compare by every field.
    friend bool operator==(const ShaderVariant&, const ShaderVariant&) = default;
};

/// A compiled material shader.
struct ShaderAsset
{
    std::string source{};                              ///< Virtual path of the `.shader` file.
    ShadingModel shadingModel = ShadingModel::Default; ///< Lighting (Default for decal shaders).
    DecalChannels decalChannels =
        DecalChannels::None;  ///< Channels of a decal shader (None: a surface shader).
    bool worldOffset = false; ///< The shader moves vertices (worldOffset()); never a decal shader.
    u32 parameterBytes = 0;   ///< Size of the parameter block (at most 256).
    std::vector<ShaderParameter> parameters{}; ///< In field order.
    std::vector<ShaderTexture> textures{};     ///< In field order (slot order).
    std::vector<ShaderVariant> variants{};     ///< Ordered by pass, then alpha mode.
    u64 libraryHash = 0;           ///< Hash of the engine's shader library it was compiled against.
    std::string compilerVersion{}; ///< Version of the slangc that compiled it.

    /// The variant of @p pass and @p mode, or nullptr if it was not compiled.
    [[nodiscard]] const ShaderVariant* variant(ShaderPass pass, AlphaMode mode) const noexcept;

    /// The parameter named @p name, or nullptr.
    [[nodiscard]] const ShaderParameter* findParameter(std::string_view name) const noexcept;

    /// The texture named @p name, or nullptr.
    [[nodiscard]] const ShaderTexture* findTexture(std::string_view name) const noexcept;

    /// Shaders compare by every field.
    friend bool operator==(const ShaderAsset&, const ShaderAsset&) = default;
};

/// The parameter block of @p shader with every default: floats as f32, ints as i32, bools as u32 0 or 1, and
/// each texture's slot (the index that bound passes sample).
[[nodiscard]] std::vector<std::byte> defaultParameterBlock(const ShaderAsset& shader);

/// Serializes @p shader as the payload of a cooked asset (AssetType::Shader).
[[nodiscard]] std::vector<std::byte> serializeShader(const ShaderAsset& shader);

/// Reads a shader written by serializeShader() in this or the previous format version (which had no decal
/// channels); std::nullopt for invalid data (including parameters or textures outside the block, too many
/// textures, a decal shader with worldOffset() or a shading model, or variants out of order or for passes
/// they do not exist in).
[[nodiscard]] std::optional<ShaderAsset> deserializeShader(std::span<const std::byte> bytes);

} // namespace illusion
