// Definitions shared by the scene shaders. Layouts must match SceneRenderer.cpp.

#include "Atmosphere.slang"

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
    float4 color;         // rgb: colour * intensity, w: 1 + its slot in ViewConstants::pointShadows (0: none)
};

static const uint kMaxShadowCascades = 4;
static const uint kMaxShadowedPointLights = 4;
// Shadow views: the cascades, then six cube faces per shadowed point light (ShadowCaster.slang).
static const uint kPointShadowFaces = 6;

// The cube shadow map of one point light (ADR 0051): perspective faces with reversed, infinite depth
// near / distance along the face axis, in layers [6 * slot, 6 * slot + 6) of the point shadow map.
struct PointShadowData
{
    float4x4 faces[kPointShadowFaces]; // world -> face clip, order +x, -x, +y, -y, +z, -z
    float4 positionNear;               // xyz: light position, w: near distance
    float4 params; // x: depth bias (m), y: normal bias (texels), z: source radius (m), w: resolution
};

// Face of a cube shadow map that holds direction @p v from the light (Shadows.cpp pointShadowFace()).
uint pointShadowFace(float3 v)
{
    const float3 a = abs(v);
    if (a.x >= a.y && a.x >= a.z)
    {
        return v.x >= 0.0 ? 0u : 1u;
    }
    if (a.y >= a.z)
    {
        return v.y >= 0.0 ? 2u : 3u;
    }
    return v.z >= 0.0 ? 4u : 5u;
}

// Projects @p position into the cube shadow map of @p data: the face, its texture coordinates and the
// distance along the face axis (at most the near distance when the point is too close to the light).
float pointShadowProject(PointShadowData data, float3 position, out uint face, out float2 uv)
{
    face = pointShadowFace(position - data.positionNear.xyz);
    const float4 clip = mul(data.faces[face], float4(position, 1.0));
    const float w = max(clip.w, 1e-6);
    uv = float2(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5);
    return clip.w;
}

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
    uint4 extra;            // x: material table index, yz: Entity, w: level of detail and its cross-fade
};

// InstanceData::extra.w (matches kLodFade* in SceneRenderer.cpp): the level of detail in bits 20-23 and,
// while an instance cross-fades between two levels, bit 17 with the fade threshold in bits 0-15; bit 16 marks
// the outgoing level.
static const uint kLodFadeActive = 1u << 17;
static const uint kLodFadeOutgoing = 1u << 16;
static const uint kLodFadeLevelShift = 20u;

// True if this copy of a cross-fading instance leaves pixel @p pixel to the other copy. Both copies test
// the same noise against the same threshold, the incoming one keeping the pixels below it and the outgoing
// one the rest, so every pixel is drawn exactly once.
bool lodFadeDiscard(uint code, float2 pixel)
{
    if ((code & kLodFadeActive) == 0u)
    {
        return false;
    }
    const float threshold = float(code & 0xFFFFu) / 65535.0;
    // Interleaved gradient noise (Jimenez 2014): evenly spread values per pixel.
    const float noise = frac(52.9829189 * frac(dot(floor(pixel), float2(0.06711056, 0.00583715))));
    return (code & kLodFadeOutgoing) != 0u ? noise < threshold : noise >= threshold;
}

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
    float4 wind;        // xyz: normalized direction, w: speed (m/s; 0 without a Wind)
    float4 windGusts;   // x: gust strength, y: gust frequency, z: turbulence, w: time
    float4 windHistory; // x: time of the previous frame
    AtmosphereData atmosphere; // the SkyAtmosphere, when atmosphereLook.z is 1
    float4 atmosphereLook;     // x: star brightness, y: seconds (twinkling), z: 1 with a SkyAtmosphere, w: moon lit fraction
    float4 fog;       // VolumetricFog (FogSample.slang): x: 1 with fog, y: near, z: slices / log(far / near), w: slices
    float4 fogScreen; // xy: 1 / render size, zw: render size in pixels
    PointShadowData pointShadows[kMaxShadowedPointLights]; // the shadowed point lights of the frame
};

// Gust factor of the wind at a world position: 1 on average, fronts travel downwind. Matches windGust() in
// engine/core/src/ecs/Wind.cpp.
float windGust(float4 wind, float4 gusts, float time, float3 position)
{
    const float phase = 2.0 * kPi * gusts.y * (time - dot(position, wind.xyz) / max(wind.w, 0.1));
    const float wave = 0.6 * sin(phase) + 0.4 * sin(2.7 * phase + 1.3);
    return max(1.0 + gusts.x * wave, 0.0);
}

// World-space offset of a vertex at @p position of an object whose origin is @p origin, for a material with
// the wind response @p response (x: MaterialData::windSway, y: windFlutter), at @p time (ADR 0048).
float3 windOffset(float4 wind, float4 gusts, float time, float2 response, float3 origin, float3 position)
{
    if (wind.w <= 0.0 || (response.x <= 0.0 && response.y <= 0.0))
    {
        return float3(0.0, 0.0, 0.0);
    }
    // The whole object bends with the gust at its origin, plus a slow sway of its own.
    const float speed = wind.w * windGust(wind, gusts, time, origin);
    const float seed = dot(origin, float3(0.37, 0.11, 0.53));
    const float height = max(position.y - origin.y, 0.0);
    const float wobble = 1.0 + 0.3 * sin(time * 1.9 + seed * 6.0);
    const float3 across = float3(wind.x, 0.0, wind.z);
    float3 offset = across * (response.x * height * height * speed * wobble);
    // A bending stem keeps its length: the tip sinks as it leans.
    const float lean = length(offset);
    offset.y -= min(lean * lean / (2.0 * max(height, 0.05)), 0.5 * height);
    // Leaves and blades flutter fast, each with its own phase.
    const float3 p = position * 2.3 + seed;
    const float3 flutter = float3(sin(time * 9.1 + p.x + p.y * 1.3), 0.5 * sin(time * 7.7 + p.y + p.z * 1.7),
                                  sin(time * 8.3 + p.z + p.x * 1.1));
    return offset + flutter * (response.y * gusts.z * speed);
}

// Tangent-space normal from the red and green channels of a normal map with z rebuilt, so that two-channel
// (BC5) and RGB maps decode alike (ADR 0054). @p scale is the glTF normal scale, applied after the rebuild.
float3 decodeNormalMap(float2 encoded, float scale)
{
    const float2 xy = encoded * 2.0 - 1.0;
    return float3(xy * scale, sqrt(saturate(1.0 - dot(xy, xy))));
}

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
