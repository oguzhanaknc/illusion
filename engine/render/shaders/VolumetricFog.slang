// Volumetric fog (VolumetricFog, ADR 0050) in a froxel grid aligned with the screen: frustum cells of a few
// pixels across, with slices spaced exponentially in view depth (Wronski 2014, Hillaire 2015).
//   injectMain:    the fog medium (height falloff, drifting noise) and the light it scatters towards the
//                  camera per froxel: sky light, every directional light (the shadowed one through the
//                  cascaded shadow map, so shafts appear) and every point light (the shadowed ones through
//                  their cube shadow maps, ADR 0051), with a Henyey-Greenstein
//                  phase. The sample moves along the depth every frame and blends with the reprojected last
//                  frame, so the shafts resolve smoothly.
//   integrateMain: front to back along each column, energy-conserving: in-scattered light and transmittance
//                  from the camera to the far side of every slice.
//   applyMain:     fogs the HDR image (opaque surfaces and the sky) by the depth buffer.
// Layouts must match VolumetricFogRenderer.cpp.

#include "Common.slang"
#include "FogSample.slang"

struct FogConstants
{
    float4x4 inverseViewProjection; // unjittered clip -> world
    float4 grid;            // xyz: froxel counts, w: offset of this frame's samples inside their slice (0 to 1)
    float4 depth;           // x: near distance, y: far distance, z: log(far / near)
    float4 medium;          // rgb: albedo, w: density (extinction per metre at the base height)
    float4 shape;           // x: height falloff (per metre), y: base height, z: anisotropy g, w: ambient intensity
    float4 noise;           // x: amount, y: 1 / size, z: wind factor, w: light intensity
    float4 history;         // x: 1 when the history is valid, y: weight of the current frame
    float4 previousCamera;  // xyz: camera position of the previous frame, w: its near distance
    float4 previousForward; // xyz: camera forward axis of the previous frame, w: its log(far / near)
};

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
ConstantBuffer<FogConstants> g_fog : register(b1, space0);
Texture3D<float4> g_history : register(t0, space0); // inject: the scattering of the previous frame
Texture2D<float> g_depth : register(t1, space0);    // apply: the scene depth (reversed-Z)
Texture3D<float4> g_volume : register(t2, space0);  // integrate: this frame's scattering; apply: integrated
Texture2DArray<float> g_shadowMap : register(t4, space0);
Texture2DArray<float> g_pointShadowMap : register(t5, space0); // reversed depth: 1 - comparison = lit
SamplerState g_linearClamp : register(s0, space0);
SamplerComparisonState g_shadowCompare : register(s1, space0); // 1 = lit
[[vk::image_format("rgba16f")]] RWTexture3D<float4> g_outVolume : register(u0, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> g_outImage : register(u1, space0);

static const float kMinLightDistanceSquared = 0.04; // keeps a froxel next to a point light finite

// World position at @p viewDepth along the camera's forward axis behind screen position @p uv. Works for
// perspective and orthographic cameras: the points at clip depth 1 (near) and 0.5 span the pixel's ray.
float3 froxelWorld(float2 uv, float viewDepth)
{
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 a = mul(g_fog.inverseViewProjection, float4(ndc, 1.0, 1.0));
    const float4 b = mul(g_fog.inverseViewProjection, float4(ndc, 0.5, 1.0));
    const float3 nearPoint = a.xyz / a.w;
    const float3 farPoint = b.xyz / b.w;
    const float3 camera = g_view.cameraPosition.xyz;
    const float3 forward = g_view.cameraForward.xyz;
    const float nearDepth = dot(nearPoint - camera, forward);
    const float farDepth = dot(farPoint - camera, forward);
    return nearPoint + (farPoint - nearPoint) * ((viewDepth - nearDepth) / (farDepth - nearDepth));
}

float hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1) * 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float valueNoise(float3 x)
{
    const float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash(i), hash(i + float3(1, 0, 0)), f.x),
                     lerp(hash(i + float3(0, 1, 0)), hash(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(hash(i + float3(0, 0, 1)), hash(i + float3(1, 0, 1)), f.x),
                     lerp(hash(i + float3(0, 1, 1)), hash(i + float3(1, 1, 1)), f.x), f.y),
                f.z);
}

// Extinction per metre at @p position: full below the base height, thinning above it, patchy with noise
// that the wind carries.
float fogDensity(float3 position)
{
    float density = g_fog.medium.w * exp(-g_fog.shape.x * max(position.y - g_fog.shape.y, 0.0));
    const float amount = g_fog.noise.x;
    if (amount > 0.0)
    {
        const float time = g_view.windGusts.w;
        const float3 drift = g_view.wind.xyz * (g_view.wind.w * g_fog.noise.z * time);
        const float3 p = (position - drift) * g_fog.noise.y;
        const float n = 0.6 * valueNoise(p) + 0.4 * valueNoise(p * 2.03 + 17.1);
        const float patches = smoothstep(0.2, 0.8, n);
        density *= lerp(1.0, 2.0 * patches, amount);
    }
    return density;
}

// Henyey-Greenstein phase function for the angle between the light's travel and the view ray towards the
// camera.
float phase(float cosTheta, float g)
{
    const float g2 = g * g;
    const float denominator = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * kPi * denominator * sqrt(denominator));
}

// Visibility of the shadowed directional light at a froxel: one filtered tap of the cascade that covers it.
float fogShadow(float3 position, float viewDepth)
{
    const uint count = uint(g_view.shadow.x);
    if (count == 0 || viewDepth >= g_view.shadowBias.z)
    {
        return 1.0;
    }
    uint cascade = 0;
    while (cascade + 1 < count && viewDepth > g_view.cascades[cascade].params.x)
    {
        ++cascade;
    }
    const ShadowCascadeData data = g_view.cascades[cascade];
    const float4 clip = mul(data.viewProjection, float4(position, 1.0));
    const float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    const float depth = clip.z - g_view.shadowBias.x / data.params.z;
    if (any(uv < 0.0) || any(uv > 1.0) || depth >= 1.0)
    {
        return 1.0;
    }
    return g_shadowMap.SampleCmpLevelZero(g_shadowCompare, float3(uv, float(cascade)), depth);
}

// Visibility of the point light with shadow slot @p slot at a froxel: one filtered tap of its cube face.
float fogPointShadow(uint slot, float3 position)
{
    const PointShadowData data = g_view.pointShadows[slot];
    uint face;
    float2 uv;
    const float distance = pointShadowProject(data, position, face, uv);
    const float nearPlane = data.positionNear.w;
    if (distance <= nearPlane)
    {
        return 1.0;
    }
    const float depth = nearPlane / max(distance - data.params.x, nearPlane);
    const float layer = float(slot * kPointShadowFaces + face);
    return 1.0 - g_pointShadowMap.SampleCmpLevelZero(g_shadowCompare, float3(uv, layer), depth);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void injectMain(uint3 id: SV_DispatchThreadID)
{
    const uint3 grid = uint3(g_fog.grid.xyz);
    if (any(id >= grid))
    {
        return;
    }
    const float slices = g_fog.grid.z;
    const float2 uv = (float2(id.xy) + 0.5) / g_fog.grid.xy;
    const float viewDepth = g_fog.depth.x * exp((float(id.z) + g_fog.grid.w) / slices * g_fog.depth.z);
    const float3 position = froxelWorld(uv, viewDepth);
    const float3 camera = g_view.cameraPosition.xyz;
    const float3 ray = g_view.effects.z > 0.5 ? g_view.cameraForward.xyz : normalize(position - camera);
    const float density = fogDensity(position);
    const float g = g_fog.shape.z;

    // Sky light, as the average radiance of the SH irradiance over the up and down hemispheres.
    const float3 ambient = (shIrradiance(g_view.sh, float3(0.0, 1.0, 0.0)) +
                            shIrradiance(g_view.sh, float3(0.0, -1.0, 0.0))) *
                           (0.5 / kPi) * g_fog.shape.w;
    float3 direct = float3(0.0, 0.0, 0.0);
    const uint shadowedLight = uint(g_view.shadow.y);
    for (uint i = 0; i < g_view.directionalCount; ++i)
    {
        const DirectionalLightData light = g_view.directional[i];
        const float visibility = i == shadowedLight ? fogShadow(position, viewDepth) : 1.0;
        direct += light.color.rgb * (visibility * phase(dot(light.direction.xyz, -ray), g));
    }
    for (uint i = 0; i < g_view.pointCount; ++i)
    {
        const PointLightData light = g_view.points[i];
        const float3 toLight = light.positionRange.xyz - position;
        const float distanceSquared = max(dot(toLight, toLight), kMinLightDistanceSquared);
        // The falloff of ForwardPbr.slang: inverse square, windowed to reach zero at the range.
        const float ratio = distanceSquared / (light.positionRange.w * light.positionRange.w);
        const float window = saturate(1.0 - ratio * ratio);
        float attenuation = window * window / distanceSquared;
        const uint shadowSlot = uint(light.color.w);
        if (shadowSlot > 0u && attenuation > 0.0)
        {
            attenuation *= fogPointShadow(shadowSlot - 1u, position);
        }
        const float3 travel = -toLight * rsqrt(distanceSquared);
        direct += light.color.rgb * (attenuation * phase(dot(travel, -ray), g));
    }
    float4 current = float4(g_fog.medium.rgb * density * (ambient + direct * g_fog.noise.w), density);

    // Temporal reprojection: the same world point in the previous frame's grid.
    if (g_fog.history.x > 0.5)
    {
        const float4 clip = mul(g_view.previousViewProjection, float4(position, 1.0));
        if (clip.w > 0.0)
        {
            const float2 previousUv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
            const float previousDepth = dot(position - g_fog.previousCamera.xyz, g_fog.previousForward.xyz);
            const float previousSlice =
                log(max(previousDepth, 1e-6) / g_fog.previousCamera.w) / g_fog.previousForward.w;
            const float3 coordinate = float3(previousUv, previousSlice);
            if (all(coordinate >= 0.0) && all(coordinate <= 1.0))
            {
                const float4 history = g_history.SampleLevel(g_linearClamp, coordinate, 0.0);
                current = lerp(history, current, g_fog.history.y);
            }
        }
    }
    g_outVolume[id] = current;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void integrateMain(uint3 id: SV_DispatchThreadID)
{
    const uint3 grid = uint3(g_fog.grid.xyz);
    if (any(id.xy >= grid.xy))
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / g_fog.grid.xy;
    // Metres along the pixel's ray per metre of view depth (1 / cos of its angle to the forward axis).
    const float stretch = length(froxelWorld(uv, 2.0) - froxelWorld(uv, 1.0));
    float3 scattered = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    float sliceStart = 0.0; // the first slice reaches back to the camera
    for (uint z = 0; z < grid.z; ++z)
    {
        const float sliceEnd = g_fog.depth.x * exp(float(z + 1) / g_fog.grid.z * g_fog.depth.z);
        const float thickness = (sliceEnd - sliceStart) * stretch;
        sliceStart = sliceEnd;
        const float4 froxel = g_volume[uint3(id.xy, z)];
        const float extinction = max(froxel.a, 1e-6);
        const float sliceTransmittance = exp(-extinction * thickness);
        // Light scattered inside the slice, attenuated within it (Hillaire 2015).
        scattered += transmittance * (froxel.rgb - froxel.rgb * sliceTransmittance) / extinction;
        transmittance *= sliceTransmittance;
        g_outVolume[uint3(id.xy, z)] = float4(scattered, transmittance);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void applyMain(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(g_view.fogScreen.zw)))
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) * g_view.fogScreen.xy;
    const float depth = g_depth.Load(int3(id.xy, 0));
    float viewDepth = 1e30; // reversed-Z: nothing was drawn here, so the sky is beyond the fog
    if (depth > 0.0)
    {
        const float4 world =
            mul(g_view.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
        viewDepth = dot(world.xyz / world.w - g_view.cameraPosition.xyz, g_view.cameraForward.xyz);
    }
    const float4 fog = sampleFog(g_volume, g_linearClamp, g_view.fog, uv, viewDepth);
    const float4 color = g_outImage[id.xy];
    g_outImage[id.xy] = float4(color.rgb * fog.a + fog.rgb, color.a);
}
