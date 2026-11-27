// A surface that dissolves: noise decides where the material is cut away as progress grows, and the cut
// edge glows. Use it with a material whose alphaMode is "Mask".
struct Parameters
{
    [Default(0.4)] [Range(0, 1)] float progress;
    [Color(1, 0.5, 0.1, 1)] float3 edgeColor;
    [Default(0.06)] float edgeWidth;
    [Default(6)] float scale;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float n = fractalNoise(input.worldPosition * p.scale, 3);
    const float edge = 1.0 - saturate((n - p.progress) / max(p.edgeWidth, 0.001));
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.6, 0.55, 0.5);
    output.roughness = 0.8;
    output.emission = p.edgeColor * edge * 4.0;
    output.opacity = n;
    output.opacityMask = p.progress;
    return output;
}
