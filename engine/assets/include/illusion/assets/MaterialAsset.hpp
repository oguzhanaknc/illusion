#pragma once

/// @file
/// Material assets: `.material` files that describe a PBR material by field name and refer to their
/// textures by asset path, so scenes can share and override materials without a model file.
///
/// ```json
/// {
///   "format": "illusion.material",
///   "version": 2,
///   "shader": "assets:/shaders/glow.shader",
///   "material": {"baseColorFactor": [0.8, 0.1, 0.1, 1], "roughnessFactor": 0.4, "metallicFactor": 0},
///   "textures": {"baseColor": "game:/textures/paint.png", "normal": "game:/textures/paint_n.png"},
///   "parameters": {"intensity": 2.5, "tint": [1, 0.5, 0.25, 1], "mask": "game:/textures/mask.png"}
/// }
/// ```
///
/// The members of "material" are those of MaterialData ("illusion.MaterialData"); missing ones keep their
/// defaults. Its `...Texture` members only set the wrap modes: the images come from "textures". Read and
/// write the files with reflectAssetStructs() called once at startup.
///
/// Version 2 (ADR 0063) adds "shader", a `.shader` file that draws the material (none: the standard shader),
/// and "parameters", the values of the shader's parameters by name: numbers, `true`/`false`, arrays of two to
/// four numbers, colours as arrays or `"#rrggbb"` / `"#rrggbbaa"` (sRGB), and the asset paths of its textures
/// (`""`: no image). Version 1 files have neither and read as they are. What the names and types must be
/// depends on the shader, so resolveMaterialParameters() checks them once the shader is compiled; names it
/// does not declare are errors, parameters the file leaves out take the shader's defaults.

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/assets/ShaderAsset.hpp>
#include <illusion/assets/TextureCompression.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// File extension of material assets.
inline constexpr std::string_view kMaterialAssetExtension = ".material";

/// Version of the material file format written by writeMaterialAsset().
inline constexpr u32 kMaterialAssetVersion = 2;

/// The textures of a material asset, by asset path (reflected as "illusion.MaterialTextures").
struct MaterialTextures
{
    AssetRef baseColor{};         ///< sRGB base colour (+ alpha).
    AssetRef metallicRoughness{}; ///< Linear: roughness in G, metalness in B.
    AssetRef normal{};            ///< Tangent-space normal map (linear).
    AssetRef occlusion{};         ///< Linear: occlusion in R.
    AssetRef emissive{};          ///< sRGB emissive colour.

    /// Compares every reference.
    friend bool operator==(const MaterialTextures&, const MaterialTextures&) = default;
};

/// A texture of a material and how its image is imported.
struct MaterialTextureUse
{
    AssetRef texture{}; ///< The image (may be empty).
    bool srgb = false;  ///< Colour data (base colour, emissive); false for linear data.
    /// BC7 for base colour, emissive and metallic-roughness, BC5 for normals, BC4 for occlusion (BC7 when it
    /// is the metallic-roughness file).
    TextureCompression compression = TextureCompression::None;
};

/// The textures of @p textures in the order baseColor, metallicRoughness, normal, occlusion, emissive, with
/// the import settings each needs (renderers and cookAssets() load them with these settings).
[[nodiscard]] std::array<MaterialTextureUse, 5>
materialTextureUses(const MaterialTextures& textures) noexcept;

/// The content of a `.material` file (reflected as "illusion.MaterialAsset").
struct MaterialAsset
{
    MaterialData material;     ///< Parameters; texture image indices are ignored (see textures).
    MaterialTextures textures; ///< Texture images.
    AssetRef shader{};         ///< The material shader (a `.shader` file); empty for the standard shader.
    /// The shader's parameters by name: a JSON object (see the file documentation; `{}` for none).
    JsonText parameters{"{}"};
};

/// A numeric or boolean shader parameter of a material, by name.
struct MaterialParameterValue
{
    std::string name{};                               ///< The shader's field.
    std::array<f32, 4> value{0.0f, 0.0f, 0.0f, 0.0f}; ///< Components (ints and bools as numbers).
    bool set = false;                                 ///< The file gives it (otherwise the shader's default).
};

/// A texture parameter of a material, by name.
struct MaterialTextureParameter
{
    std::string name{}; ///< The shader's texture field.
    AssetRef texture{}; ///< The image: the file's, else the shader's default image (empty: the fallback).
    MaterialTextureUse use{}; ///< How the image is imported (the usage of the shader's texture).
    bool set = false;         ///< The file gives it (otherwise the shader's default image).
};

/// The parameters of a MaterialAsset checked against its shader, in the shader's field order.
struct MaterialParameters
{
    std::vector<MaterialParameterValue> values{};     ///< Every numeric parameter of the shader.
    std::vector<MaterialTextureParameter> textures{}; ///< Every texture of the shader.
};

/// Checks "parameters" of @p material against the parameters and textures @p shader declares and returns
/// every one with its value (the file's, or the shader's default). Fails with a message that names the JSON
/// Pointer of the first problem: a name the shader does not declare (the message lists the declared ones), a
/// value of the wrong type or size, or "parameters" that is not an object.
[[nodiscard]] Expected<MaterialParameters, std::string>
resolveMaterialParameters(const MaterialAsset& material, const ShaderAsset& shader);

/// How an image of the texture @p texture of a material shader is imported: sRGB for the usage Color, BC7 for
/// Color and Linear, BC5 for Normal and BC4 for Mask.
[[nodiscard]] MaterialTextureUse shaderTextureUse(const ShaderTexture& texture,
                                                  const AssetRef& image) noexcept;

/// Reads a material file. Fails with a message for invalid JSON, a wrong "format" or a "version" newer
/// than kMaterialAssetVersion, and for invalid values (with the JSON Pointer of the first one).
[[nodiscard]] Expected<MaterialAsset, std::string> parseMaterialAsset(std::string_view json);

/// Writes @p asset as a material file (indented JSON). Fails for non-finite numbers and texture ids
/// without a recorded path.
[[nodiscard]] Expected<std::string, std::string> writeMaterialAsset(const MaterialAsset& asset);

/// Decoder for AssetStreamer::loadFile() that parses material files on the Job System:
/// `streamer.loadFile<MaterialAsset>(path, kMaterialDecoderName, materialAssetDecoder())`.
[[nodiscard]] AssetFileDecoder<MaterialAsset> materialAssetDecoder();

/// Decoder name to use with materialAssetDecoder().
inline constexpr std::string_view kMaterialDecoderName = "illusion.material/1";

} // namespace illusion
