#pragma once

/// @file
/// Material assets: `.material` files that describe a PBR material by field name and refer to their
/// textures by asset path, so scenes can share and override materials without a model file.
///
/// ```json
/// {
///   "format": "illusion.material",
///   "version": 1,
///   "material": {"baseColorFactor": [0.8, 0.1, 0.1, 1], "roughnessFactor": 0.4, "metallicFactor": 0},
///   "textures": {"baseColor": "game:/textures/paint.png", "normal": "game:/textures/paint_n.png"}
/// }
/// ```
///
/// The members of "material" are those of MaterialData ("illusion.MaterialData"); missing ones keep their
/// defaults. Its `...Texture` members only set the wrap modes: the images come from "textures". Read and
/// write the files with reflectAssetStructs() called once at startup.

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Expected.hpp>

#include <array>
#include <string>
#include <string_view>

namespace illusion
{

/// File extension of material assets.
inline constexpr std::string_view kMaterialAssetExtension = ".material";

/// Version of the material file format written by writeMaterialAsset().
inline constexpr u32 kMaterialAssetVersion = 1;

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
};

/// The textures of @p textures in the order baseColor, metallicRoughness, normal, occlusion, emissive, with
/// the import setting each needs (renderers and cookAssets() load them with these settings).
[[nodiscard]] std::array<MaterialTextureUse, 5>
materialTextureUses(const MaterialTextures& textures) noexcept;

/// The content of a `.material` file (reflected as "illusion.MaterialAsset").
struct MaterialAsset
{
    MaterialData material;     ///< Parameters; texture image indices are ignored (see textures).
    MaterialTextures textures; ///< Texture images.
};

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
