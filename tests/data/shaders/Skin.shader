// A subsurface material for the renderer's tests (TestMaterialShaders.cpp): pale skin whose light wraps past the
// terminator in a red scatter colour; with thickness 1 it shades like the Default model.

[ShadingModel("Subsurface")]
struct Parameters
{
    [Color(0.9, 0.7, 0.6, 1)] float3 baseColor;
    [Color(1, 0.25, 0.1, 1)] float3 scatterColor;
    [Default(0.2)] [Range(0, 1)] float thickness;
    [Default(0.6)] [Range(0, 1)] float roughness;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = p.baseColor;
    output.roughness = p.roughness;
    output.subsurfaceColor = p.scatterColor;
    output.thickness = p.thickness;
    return output;
}
