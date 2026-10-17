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

static const uint kMaxShadowCascades = 4;

struct ShadowCascadeData
{
    float4x4 viewProjection; // world -> shadow clip (depth 0 nearest the light)
    float4 params;           // x: split far distance, y: world size, z: depth range
};

struct InstanceData
{
    float4x4 world;
    float4x4 normalMatrix;
    float4x4 previousWorld; // world matrix of the previous frame, for motion vectors
    uint4 extra;            // x: material table index
};

struct ViewConstants
{
    float4x4 viewProjection;
    float4 cameraPosition; // xyz
    float4 effects;        // x: 1 if screen-space ambient occlusion is applied
    uint directionalCount;
    uint pointCount;
    uint2 padding;
    DirectionalLightData directional[kMaxDirectionalLights];
    PointLightData points[kMaxPointLights];
    float4x4 inverseViewProjection;
    float4x4 skyRotation; // world direction -> environment direction
    float4 sky;           // x: specular intensity, y: last specular mip, z: background intensity
    float4 sh[9];         // diffuse irradiance (SH, intensity applied), rgb
    ShadowCascadeData cascades[kMaxShadowCascades];
    float4 shadow;        // x: cascade count (0 = off), y: shadowed directional light, z: tan(light angular radius), w: map resolution
    float4 shadowBias;    // x: depth bias (world), y: normal bias (texels), z: max distance
    float4 cameraForward; // xyz: camera forward axis
    float4x4 unjitteredViewProjection;
    float4x4 previousViewProjection; // unjittered, previous frame
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
