// Definitions shared by the scene shaders. Layouts must match SceneRenderer.cpp.

static const uint kMaxDirectionalLights = 4;
static const uint kMaxPointLights = 16;
static const float kPi = 3.14159265358979;

struct DirectionalLightData
{
    float4 direction; // xyz: direction the light travels (normalized)
    float4 color;     // rgb: colour * intensity
};

struct PointLightData
{
    float4 positionRange; // xyz: position, w: range
    float4 color;         // rgb: colour * intensity
};

struct ViewConstants
{
    float4x4 viewProjection;
    float4 cameraPosition; // xyz; w: exposure
    float4 output;         // x: 1 if the shader must encode sRGB itself
    uint directionalCount;
    uint pointCount;
    uint2 padding;
    DirectionalLightData directional[kMaxDirectionalLights];
    PointLightData points[kMaxPointLights];
    float4x4 inverseViewProjection;
    float4x4 skyRotation; // world direction -> environment direction
    float4 sky;           // x: specular intensity, y: last specular mip, z: background intensity
    float4 sh[9];         // diffuse irradiance (SH, intensity applied), rgb
};

// Irradiance from the 9 SH coefficients (order Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22); see
// assets/Environment.cpp.
float3 shIrradiance(float4 sh[9], float3 n)
{
    float3 e = sh[0].rgb * 0.282095;
    e += sh[1].rgb * (0.488603 * n.y);
    e += sh[2].rgb * (0.488603 * n.z);
    e += sh[3].rgb * (0.488603 * n.x);
    e += sh[4].rgb * (1.092548 * n.x * n.y);
    e += sh[5].rgb * (1.092548 * n.y * n.z);
    e += sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
    e += sh[7].rgb * (1.092548 * n.x * n.z);
    e += sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(e, 0.0);
}

float3 tonemapAces(float3 x)
{
    // Narkowicz 2015 fit of the ACES filmic curve.
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

// Exposure, tonemapping and (for UNORM targets) sRGB encoding.
float3 finalColor(ViewConstants view, float3 color)
{
    float3 mapped = tonemapAces(color * view.cameraPosition.w);
    if (view.output.x > 0.5)
    {
        mapped = linearToSrgb(mapped);
    }
    return mapped;
}
