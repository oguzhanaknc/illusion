// The error material, builtin:/shader/error (ADR 0063): an unlit magenta and black checker in world space that
// tools give materials whose shader does not compile, so they stand out in any light.

[ShadingModel("Unlit")]
struct Parameters
{
    [Default(4)] [Range(0.1, 64)] float cellsPerMetre;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const int3 cell = int3(floor(input.worldPosition * max(p.cellsPerMetre, 0.1)));
    const bool odd = ((cell.x + cell.y + cell.z) & 1) != 0;
    SurfaceOutput output = defaultSurface();
    output.baseColor = odd ? float3(1.0, 0.0, 1.0) : float3(0.05, 0.0, 0.05);
    return output;
}
