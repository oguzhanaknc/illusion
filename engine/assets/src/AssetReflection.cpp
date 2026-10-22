#include <illusion/assets/AssetReflection.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/ecs/StructRegistry.hpp>

namespace illusion
{

void reflectAssetStructs(ComponentRegistry& registry)
{
    StructRegistry& structs = registry.structs();
    reflectStruct<ImageImportSettings>("illusion.ImageImportSettings", 1, structs)
        .meta({.displayName = "Image import", .docsAnchor = "manual/assets.md"})
        .field("srgb", &ImageImportSettings::srgb,
               {.displayName = "sRGB", .description = "Colour data; turn off for normal maps and masks."})
        .field("generateMips", &ImageImportSettings::generateMips);
    reflectStruct<SkyDesc>("illusion.SkyDesc", 1, structs)
        .meta({.displayName = "Procedural sky", .docsAnchor = "manual/rendering.md#image-based-lighting"})
        .field("zenith", &SkyDesc::zenith, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("horizon", &SkyDesc::horizon, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("ground", &SkyDesc::ground, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("sunDirection", &SkyDesc::sunDirection)
        .field("sunRadiance", &SkyDesc::sunRadiance, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("sunAngularRadius", &SkyDesc::sunAngularRadius,
               {.unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 0.5,
                .hint = FieldHint::Angle});
    reflectStruct<TextureRef>("illusion.TextureRef", 1, structs)
        .meta({.displayName = "Texture"})
        .field("image", &TextureRef::image,
               {.description = "Index of the model image; -1 for none.", .min = -1.0})
        .field("wrapU", &TextureRef::wrapU)
        .field("wrapV", &TextureRef::wrapV);
    reflectStruct<MaterialData>("illusion.MaterialData", 1, structs)
        .meta({.displayName = "Material", .docsAnchor = "manual/rendering.md#materials-and-shading"})
        .field("name", &MaterialData::name)
        .field("baseColorFactor", &MaterialData::baseColorFactor, {.min = 0.0, .hint = FieldHint::Color})
        .field("metallicFactor", &MaterialData::metallicFactor,
               {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("roughnessFactor", &MaterialData::roughnessFactor,
               {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("emissiveFactor", &MaterialData::emissiveFactor, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("normalScale", &MaterialData::normalScale)
        .field("occlusionStrength", &MaterialData::occlusionStrength,
               {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("alphaCutoff", &MaterialData::alphaCutoff, {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("alphaMode", &MaterialData::alphaMode)
        .field("doubleSided", &MaterialData::doubleSided)
        .field("baseColorTexture", &MaterialData::baseColorTexture)
        .field("metallicRoughnessTexture", &MaterialData::metallicRoughnessTexture)
        .field("normalTexture", &MaterialData::normalTexture)
        .field("occlusionTexture", &MaterialData::occlusionTexture)
        .field("emissiveTexture", &MaterialData::emissiveTexture);
    reflectStruct<MaterialTextures>("illusion.MaterialTextures", 1, structs)
        .meta({.displayName = "Material textures", .docsAnchor = "manual/assets.md#material-assets"})
        .field("baseColor", &MaterialTextures::baseColor, {.assetKind = AssetKind::Texture})
        .field("metallicRoughness", &MaterialTextures::metallicRoughness, {.assetKind = AssetKind::Texture})
        .field("normal", &MaterialTextures::normal, {.assetKind = AssetKind::Texture})
        .field("occlusion", &MaterialTextures::occlusion, {.assetKind = AssetKind::Texture})
        .field("emissive", &MaterialTextures::emissive, {.assetKind = AssetKind::Texture});
    reflectStruct<MaterialAsset>("illusion.MaterialAsset", 1, structs)
        .meta({.displayName = "Material asset", .docsAnchor = "manual/assets.md#material-assets"})
        .field("material", &MaterialAsset::material)
        .field("textures", &MaterialAsset::textures);
}

} // namespace illusion
