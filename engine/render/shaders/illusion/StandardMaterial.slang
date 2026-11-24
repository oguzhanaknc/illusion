// The surface of the built-in standard materials (ADR 0063): glTF 2.0 metallic-roughness PBR with diffuse
// transmission (KHR_materials_diffuse_transmission) and the scene's wind. builtin:/shader/standard and
// builtin:/shader/foliage include it after defining ILLUSION_STANDARD_SHADING_MODEL. The renderer fills the
// parameters from MaterialData (RenderResources.cpp); the names and texture order are those of MaterialData and
// MaterialTextures.

#pragma once

[ShadingModel(ILLUSION_STANDARD_SHADING_MODEL)]
struct Parameters
{
    [Color(1, 1, 1, 1)] float4 baseColorFactor;
    [Color(0, 0, 0, 1)] float3 emissiveFactor;
    [Default(1)] float normalScale;
    [Default(1)] [Range(0, 1)] float metallicFactor;
    [Default(1)] [Range(0, 1)] float roughnessFactor;
    [Default(1)] [Range(0, 1)] float occlusionStrength;
    [Default(0.5)] [Range(0, 1)] float alphaCutoff;
    [Color(1, 1, 1, 1)] float3 diffuseTransmissionColor;
    [Default(0)] [Range(0, 1)] float diffuseTransmission;
    [Default(0)] float windSway;
    [Default(0)] float windFlutter;
    [Texture("white", "color")] Texture2DSlot baseColorTexture;
    [Texture("white", "linear")] Texture2DSlot metallicRoughnessTexture;
    [Texture("normal", "normal")] Texture2DSlot normalTexture;
    [Texture("white", "mask")] Texture2DSlot occlusionTexture;
    [Texture("white", "color")] Texture2DSlot emissiveTexture;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float4 baseColor = sampleTexture(input, p.baseColorTexture, input.uv) * p.baseColorFactor;
    // Roughness in green, metalness in blue (glTF).
    const float4 metallicRoughness = sampleTexture(input, p.metallicRoughnessTexture, input.uv);
    SurfaceOutput output = defaultSurface();
    output.baseColor = baseColor.rgb;
    output.opacity = baseColor.a;
    output.opacityMask = p.alphaCutoff;
    output.normal = unpackNormal(sampleTexture(input, p.normalTexture, input.uv), p.normalScale);
    output.metallic = metallicRoughness.b * p.metallicFactor;
    output.roughness = metallicRoughness.g * p.roughnessFactor;
    output.occlusion = lerp(1.0, sampleTexture(input, p.occlusionTexture, input.uv).r, p.occlusionStrength);
    output.emission = sampleTexture(input, p.emissiveTexture, input.uv).rgb * p.emissiveFactor;
    output.subsurfaceColor = p.diffuseTransmissionColor;
    output.thickness = 1.0 - p.diffuseTransmission;
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    const Parameters p = parameters(input);
    return windOffset(input, p.windSway, p.windFlutter);
}
