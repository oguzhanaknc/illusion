// Glow.shader after an edit, for the hot reload test: a new parameter before the others moves every offset, and
// the whole surface glows (no stripes).

struct Parameters
{
    [Default(1)] float pulse;
    [Color(1, 0.4, 0.1, 1)] float3 glowColor;
    [Default(4)] [Range(0, 20)] float intensity;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.1, 0.1, 0.1);
    output.emission = p.glowColor * p.intensity * p.pulse * sampleTexture(input, p.mask, input.uv).r;
    return output;
}
