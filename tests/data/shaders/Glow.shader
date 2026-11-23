// A custom emissive material for the renderer's tests (TestMaterialShaders.cpp): glowing stripes over a dark
// base tinted by the vertex colour; with `cutout`, alpha-masked between the stripes.

struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float3 glowColor;
    [Default(4)] [Range(0, 20)] float intensity;
    [Default(6)] float stripes;
    [Default(0)] bool cutout;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float band = step(0.5, frac(input.uv.y * p.stripes));
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.1, 0.1, 0.1) * input.color.rgb;
    output.emission = p.glowColor * p.intensity * band * sampleTexture(input, p.mask, input.uv).r;
    output.opacity = p.cutout ? band : 1.0;
    return output;
}
