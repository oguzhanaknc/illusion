// A pulsing emissive material that declares every parameter type: a float with a range, a vector, two colours,
// an int, a bool and a texture. The material pulse.material sets each of them.
struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float4 tint;
    [Color(0.1, 0.1, 0.1, 1)] float3 base;
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
    output.baseColor = p.base;
    output.emission = p.tint.rgb * p.intensity * band * beat * sampleTexture(input, p.mask, uv).r;
    return output;
}
