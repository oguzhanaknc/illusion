// An unlit material for the renderer's tests (TestMaterialShaders.cpp): a colour gradient over the UVs that no
// light, shadow or sky changes.

[ShadingModel("Unlit")]
struct Parameters
{
    [Color(1, 0.3, 0.1, 1)] float3 top;
    [Color(0.1, 0.3, 1, 1)] float3 bottom;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = lerp(p.top, p.bottom, saturate(input.uv.y));
    return output;
}
