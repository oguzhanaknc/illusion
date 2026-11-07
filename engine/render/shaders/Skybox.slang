// Environment background: a full-screen triangle at the far plane (depth 0 with reversed-Z), drawn
// after opaque geometry so the depth test rejects covered pixels.

#include "Common.slang"

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
TextureCube g_skybox : register(t3, space0);
SamplerState g_clampSampler : register(s0, space0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 clip : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 clip = float2(float((vertexId << 1) & 2), float(vertexId & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    output.clip = clip;
    return output;
}

static const float kSunAngularRadius = 0.00465;
static const float kMoonAngularRadius = 0.018; // about four times the real moon, as games and photos show it

uint skyHash(uint3 v)
{
    uint h = v.x * 73856093u ^ v.y * 19349663u ^ v.z * 83492791u;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

float skyRandom(uint3 v, uint salt)
{
    return float(skyHash(v + uint3(salt, salt * 7u, salt * 13u)) >> 8) * (1.0 / 16777216.0);
}

// Smooth value noise of a direction, for the Milky Way and the moon's seas.
float skyNoise(float3 p)
{
    const float3 cell = floor(p);
    const float3 f = p - cell;
    const float3 w = f * f * (3.0 - 2.0 * f);
    const int3 c = int3(cell) + 4096;
    float result = 0.0;
    for (uint corner = 0; corner < 8; ++corner)
    {
        const uint3 o = uint3(corner & 1u, (corner >> 1) & 1u, (corner >> 2) & 1u);
        const float3 weight = lerp(1.0 - w, w, float3(o));
        result += weight.x * weight.y * weight.z * skyRandom(uint3(c) + o, 3u);
    }
    return result;
}

// Stars on a grid over the six cube faces: at most one per cell, with its own brightness, colour and
// twinkle. `pixel` is the angular size of a pixel, so stars stay a pixel or two wide at any resolution.
float3 stars(float3 d, float seconds, float pixel)
{
    const float3 a = abs(d);
    uint face;
    float2 uv;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x > 0.0 ? 0u : 1u;
        uv = d.yz / a.x;
    }
    else if (a.y >= a.z)
    {
        face = d.y > 0.0 ? 2u : 3u;
        uv = d.xz / a.y;
    }
    else
    {
        face = d.z > 0.0 ? 4u : 5u;
        uv = d.xy / a.z;
    }
    static const float kCells = 220.0;
    const float2 grid = (uv * 0.5 + 0.5) * kCells;
    const float2 cell = floor(grid);
    const uint3 key = uint3(uint2(cell), face);
    const float presence = skyRandom(key, 1u);
    if (presence > 0.12)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float2 centre = cell + 0.15 + 0.7 * float2(skyRandom(key, 2u), skyRandom(key, 5u));
    // A cell spans about 2 / kCells radians.
    const float distance = length(grid - centre) * (2.0 / kCells);
    const float width = max(pixel * 0.7, 2e-4);
    const float magnitude = pow(skyRandom(key, 7u), 8.0); // many faint stars, few bright ones
    const float twinkle = 0.75 + 0.25 * sin(seconds * (2.0 + 4.0 * skyRandom(key, 11u)) + 40.0 * presence);
    const float temperature = skyRandom(key, 13u);
    const float3 colour = lerp(float3(1.0, 0.75, 0.55), float3(0.7, 0.8, 1.0), temperature);
    return colour * (magnitude * 5.0 * twinkle * exp(-distance * distance / (2.0 * width * width)));
}

// A faint band of light across the night sky.
float3 milkyWay(float3 d)
{
    const float3 normal = normalize(float3(0.3, 0.55, 0.78));
    const float band = exp(-pow(dot(d, normal), 2.0) * 28.0);
    const float clouds = skyNoise(d * 9.0) * 0.6 + skyNoise(d * 23.0) * 0.4;
    return float3(0.012, 0.012, 0.018) * band * smoothstep(0.35, 0.8, clouds);
}

// The moon's disc, lit from the side the sun is on, with darker seas.
float3 moonDisc(float3 d, float3 moon, float3 sun)
{
    const float cosine = dot(d, moon);
    const float cosRadius = cos(kMoonAngularRadius);
    if (cosine < cosRadius)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 offset = (d - moon * cosine) / sin(kMoonAngularRadius); // in the disc, length up to 1
    const float r2 = saturate(dot(offset, offset));
    const float3 normal = offset - moon * sqrt(1.0 - r2); // the face we see points back at us
    const float lit = smoothstep(-0.05, 0.1, dot(normal, sun));
    const float seas = 0.65 + 0.35 * smoothstep(0.3, 0.7, skyNoise(offset * 4.0 + 17.0));
    const float edge = 1.0 - smoothstep(0.92, 1.0, sqrt(r2));
    return float3(1.0, 0.97, 0.9) * (lit * seas * edge * 2.5 + 0.004 * edge); // earthshine on the dark side
}

float3 atmosphereBackground(float3 d, float pixel)
{
    const AtmosphereData a = g_view.atmosphere;
    float3 radiance = atmosphereRadiance(a, d);
    if (d.y <= 0.0)
    {
        return radiance;
    }
    const float3 through = atmosphereTransmittance(a, d);
    const float scale = max(a.sun.w, 0.0);
    // The sun's disc, darker at its limb; bright enough to bloom.
    const float sunCosine = dot(d, a.sun.xyz);
    const float sunEdge = cos(kSunAngularRadius + pixel);
    if (sunCosine > sunEdge)
    {
        const float r = saturate(acos(min(sunCosine, 1.0)) / kSunAngularRadius);
        const float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - r * r, 0.0)));
        radiance += through * (4000.0 * scale * limb * smoothstep(sunEdge, cos(kSunAngularRadius), sunCosine));
    }
    // The sky outshines the moon and stars by day.
    const float night = g_view.atmosphereLook.x;
    radiance += through * moonDisc(d, a.moon.xyz, a.sun.xyz) * scale * lerp(0.15, 1.0, saturate(night));
    radiance += through * (stars(d, g_view.atmosphereLook.y, pixel) + milkyWay(d)) * night * scale;
    return radiance;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Unproject a point on the near plane (depth 1) and look from the camera through it.
    const float4 nearPoint = mul(g_view.inverseViewProjection, float4(input.clip, 1.0, 1.0));
    const float3 direction = (g_view.effects.z > 0.5 ? g_view.cameraForward.xyz : normalize(nearPoint.xyz / nearPoint.w - g_view.cameraPosition.xyz));
    if (g_view.atmosphereLook.z > 0.5)
    {
        // The angle between neighbouring pixels.
        const float pixel = max(length(ddx(direction)), length(ddy(direction)));
        return float4(atmosphereBackground(direction, pixel), 1.0);
    }
    const float3 environmentDirection = mul((float3x3)g_view.skyRotation, direction);
    const float3 radiance = g_skybox.SampleLevel(g_clampSampler, environmentDirection, 0.0).rgb * g_view.sky.z;
    return float4(radiance, 1.0);
}
