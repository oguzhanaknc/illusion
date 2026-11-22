// A clear-coated material for the renderer's tests (TestMaterialShaders.cpp): rough red paint under a glossy
// coat; with coat 0 it shades like the Default model.

[ShadingModel("ClearCoat")]
struct Parameters
{
    [Color(0.6, 0.05, 0.05, 1)] float3 baseColor;
    [Default(0.7)] [Range(0, 1)] float roughness;
    [Default(1)] [Range(0, 1)] float coat;
    [Default(0.08)] [Range(0, 1)] float coatRoughness;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = p.baseColor;
    output.roughness = p.roughness;
    output.clearCoat = p.coat;
    output.clearCoatRoughness = p.coatRoughness;
    return output;
}
