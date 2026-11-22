// A wind-like world offset for the renderer's tests (TestMaterialShaders.cpp): bends the object along +x with
// the square of the height above its origin, swaying over time.

struct Parameters
{
    [Color(0.2, 0.7, 0.3, 1)] float3 color;
    [Default(0.4)] float bend;
    [Default(2)] float frequency;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.baseColor = p.color;
    output.roughness = 0.8;
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    const Parameters p = parameters(input);
    const float height = max(input.worldPosition.y - input.objectOrigin.y, 0.0);
    return float3(p.bend * height * height * (0.75 + 0.25 * sin(input.time * p.frequency)), 0.0, 0.0);
}
