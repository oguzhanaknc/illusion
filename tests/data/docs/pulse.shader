// A pulsing emissive surface: stripes of light scroll over a dark body and a hanging cloth sways in the wind.
[ShadingModel("Default")]
struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float3 glow;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(3)] int bands;
    [Default(1)] bool pulse;
    [Default2(0.5, 0.25)] float2 scroll;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float2 uv = input.uv + p.scroll * input.time;
    const float band = step(0.5, frac(uv.y * float(p.bands)));
    const float beat = p.pulse ? 0.5 + 0.5 * sin(input.time * 3.0) : 1.0;
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.08, 0.08, 0.1);
    output.emission = p.glow * p.intensity * band * beat * sampleTexture(input, p.mask, uv).r;
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    return windOffset(input, 0.05, 0.002);
}
