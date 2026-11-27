// The standard decal, builtin:/shader/decal (ADR 0066): base colour with its opacity in alpha, a normal map, an
// occlusion-roughness-metallic map and emission, every channel, from one frame of an atlas picked by the decal's
// variant (bullet holes 1 to 4 in one texture).

[Decal("BaseColor Normal ORM Emission")]
struct Parameters
{
    [Color(1, 1, 1, 1)] float4 baseColorFactor;
    [Color(0, 0, 0, 1)] float3 emissiveFactor;
    [Default(1)] float normalScale;
    [Default(1)] [Range(0, 1)] float occlusionStrength;
    [Default(1)] [Range(0, 1)] float roughnessFactor;
    [Default(0)] [Range(0, 1)] float metallicFactor;
    [Default(1)] [Range(1, 16)] int atlasColumns;
    [Default(1)] [Range(1, 16)] int atlasRows;
    [Texture("white", "color")] Texture2DSlot baseColorTexture;
    [Texture("normal", "normal")] Texture2DSlot normalTexture;
    [Texture("white", "linear")] Texture2DSlot ormTexture; // occlusion in red, roughness in green, metallic in blue
    [Texture("white", "color")] Texture2DSlot emissiveTexture;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float2 uv =
        decalAtlasUv(input.uv, decalVariant(input), uint(max(p.atlasColumns, 1)), uint(max(p.atlasRows, 1)));
    const float4 baseColor = sampleTexture(input, p.baseColorTexture, uv) * p.baseColorFactor * input.color;
    const float3 orm = sampleTexture(input, p.ormTexture, uv).rgb;
    SurfaceOutput output = defaultSurface();
    output.baseColor = baseColor.rgb;
    output.opacity = baseColor.a;
    output.normal = unpackNormal(sampleTexture(input, p.normalTexture, uv), p.normalScale);
    output.occlusion = lerp(1.0, orm.r, p.occlusionStrength);
    output.roughness = orm.g * p.roughnessFactor;
    output.metallic = orm.b * p.metallicFactor;
    output.emission = sampleTexture(input, p.emissiveTexture, uv).rgb * p.emissiveFactor;
    return output;
}
