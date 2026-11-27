// The passes of decal shaders (ADR 0066). ShaderCompiler compiles illusion/Surface.slang, illusion/Decal.slang,
// the `.shader` source and this file once per variant, with the entry points vertexMain and fragmentMain and the
// defines
//   ILLUSION_PASS_DECAL (into the decal buffer) or ILLUSION_PASS_DECAL_EMISSION (emission into the HDR colour);
//   ILLUSION_DECAL_CHANNELS: the shader's DecalChannels (ShaderAsset.hpp);
//   ILLUSION_MATERIAL_PARAMETERS: Parameters has fields.
// A draw is one instance per decal of a unit box of 36 vertices from SV_VertexID, without vertex buffers; the box's
// faces wind counter-clockwise seen from outside (right-handed), so the pipeline culls front faces and tests no
// depth, and a camera inside the box still sees it. Bindings, which SceneRenderer.cpp must match: space 0 holds the
// view at b0, the decal records at t0, the scene depth (reversed-Z) at t1 and the receiver mask (R8_UINT, the decal
// layers of the surface) at t2, and the push constants the first record of the draw; space 1 holds the material as
// in the bound material passes: parameters at b0, textures at t0..t7, one sampler at s0.

#include "Surface.slang"
#include "Decal.slang"

#if !defined(ILLUSION_DECAL_CHANNELS)
#error "decal passes need ILLUSION_DECAL_CHANNELS"
#endif

static const uint kMaxMaterialTextures = 8;
static const uint kDecalChannels = ILLUSION_DECAL_CHANNELS;
static const uint kDecalNormalChannel = 2u;

// One decal of the frame, 160 bytes.
struct DecalRecord
{
    float4 decalToWorld[3]; // rows of the 3x4 matrix from the unit box [-1/2, 1/2]^3 to world space (size included)
    float4 worldToDecal[3]; // rows of its inverse
    float4 color;           // Decal::color, linear
    float4 fades;           // x: cos(angleFadeStart), y: cos(angleFadeEnd), z: depthFade, w: drawDistance (0: none)
    float4 state;           // x: age in seconds, y: life fraction, z: fade weight, w: opacity
    uint4 info;             // x: receiver layers (bits 0-7), y: material, z: variant, w: 0
};

struct DecalPush
{
    uint firstDecal; // record of the draw's first instance
    uint3 padding;
};

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
StructuredBuffer<DecalRecord> g_decals : register(t0, space0);
Texture2D<float> g_depth : register(t1, space0);
Texture2D<uint> g_receiverMask : register(t2, space0);
[[vk::push_constant]] ConstantBuffer<DecalPush> g_push;

#if defined(ILLUSION_MATERIAL_PARAMETERS)
ConstantBuffer<Parameters> g_materialParameters : register(b0, space1);
#endif
Texture2D g_materialTextures[kMaxMaterialTextures] : register(t0, space1);
SamplerState g_materialSampler : register(s0, space1);

// The decal UV of the pixel and its change to the next pixel in x and y on the receiver's tangent plane, set before
// surface() runs.
static float2 s_decalUv;
static float2 s_decalUvDx;
static float2 s_decalUvDy;

// Gradients of @p uv for SampleGrad(): those of the decal UV on the receiver plane, carried over to @p uv through
// how @p uv changes with the decal UV within the pixel quad. That relation holds across depth edges for any affine
// function of the decal UV (atlas frames, tiling, scrolling); without one the decal UV's gradients are used.
void decalGradients(float2 uv, out float2 dx, out float2 dy)
{
    const float2 a = ddx(s_decalUv);
    const float2 b = ddy(s_decalUv);
    const float det = a.x * b.y - a.y * b.x;
    if (abs(det) <= 1e-4 * length(a) * length(b))
    {
        dx = s_decalUvDx;
        dy = s_decalUvDy;
        return;
    }
    const float2 ua = ddx(uv);
    const float2 ub = ddy(uv);
    const float2 wx = float2(b.y * s_decalUvDx.x - b.x * s_decalUvDx.y, a.x * s_decalUvDx.y - a.y * s_decalUvDx.x) / det;
    const float2 wy = float2(b.y * s_decalUvDy.x - b.x * s_decalUvDy.y, a.x * s_decalUvDy.y - a.y * s_decalUvDy.x) / det;
    dx = ua * wx.x + ub * wx.y;
    dy = ua * wy.x + ub * wy.y;
}

float4 illusionSampleTexture(uint material, Texture2DSlot texture, float2 uv)
{
    float2 dx;
    float2 dy;
    decalGradients(uv, dx, dy);
    return g_materialTextures[min(texture, kMaxMaterialTextures - 1)].SampleGrad(g_materialSampler, uv, dx, dy);
}

float4 illusionSampleTextureLevel(uint material, Texture2DSlot texture, float2 uv, float level)
{
    return g_materialTextures[min(texture, kMaxMaterialTextures - 1)].SampleLevel(g_materialSampler, uv, level);
}

Parameters illusionMaterialParameters(uint material)
{
#if defined(ILLUSION_MATERIAL_PARAMETERS)
    return g_materialParameters;
#else
    Parameters none;
    return none;
#endif
}

float3 transformPoint(float4 rows[3], float3 p)
{
    return float3(dot(rows[0].xyz, p) + rows[0].w, dot(rows[1].xyz, p) + rows[1].w, dot(rows[2].xyz, p) + rows[2].w);
}

// Column @p axis of the 3x3 part of @p rows: a box axis in world space.
float3 boxAxis(float4 rows[3], uint axis)
{
    return float3(rows[0][axis], rows[1][axis], rows[2][axis]);
}

// The unit box: corner i at (bit 0, bit 1, bit 2) - 1/2, two triangles per face, counter-clockwise from outside.
static const uint kBoxIndices[36] = {1, 3, 7, 1, 7, 5, 0, 4, 6, 0, 6, 2, 2, 6, 7, 2, 7, 3,
                                     0, 1, 5, 0, 5, 4, 4, 5, 7, 4, 7, 6, 0, 2, 3, 0, 3, 1};

struct DecalVaryings
{
    float4 position : SV_Position;
    nointerpolation uint decal : DECAL; // index of its record
};

[shader("vertex")]
DecalVaryings vertexMain(uint vertexId: SV_VertexID, uint instanceId: SV_InstanceID)
{
    const uint decal = g_push.firstDecal + instanceId;
    const uint corner = kBoxIndices[vertexId % 36u];
    const float3 local = float3(float(corner & 1u), float((corner >> 1u) & 1u), float((corner >> 2u) & 1u)) - 0.5;
    DecalVaryings output;
    output.position = mul(g_view.viewProjection, float4(transformPoint(g_decals[decal].decalToWorld, local), 1.0));
    output.decal = decal;
    return output;
}

float depthAt(int2 pixel)
{
    return g_depth.Load(int3(clamp(pixel, int2(0, 0), int2(g_view.fogScreen.zw) - 1), 0));
}

// World position of the point at @p depth behind pixel coordinates @p location (pixel centres at +0.5), with the
// jittered view of the frame's depth.
float3 worldAt(float2 location, float depth)
{
    const float2 uv = location * g_view.fogScreen.xy;
    const float4 world = mul(g_view.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    return world.xyz / world.w;
}

// Where the ray through @p location meets the plane through @p origin with the normal @p normal.
float3 planeHit(float2 location, float depth, float3 origin, float3 normal)
{
    const float3 start = worldAt(location, depth);
    const float3 direction = worldAt(location, depth * 0.5) - start; // reversed-Z: half the depth is farther
    const float along = dot(direction, normal);
    if (abs(along) <= 1e-6 * length(direction))
    {
        return start;
    }
    return start + direction * (dot(origin - start, normal) / along);
}

float2 decalUv(DecalRecord record, float3 world)
{
    const float3 local = transformPoint(record.worldToDecal, world);
    return float2(local.x + 0.5, 0.5 - local.y);
}

// The surface of the receiver at @p input, with the decal's surface() evaluated on it; @p weight is the coverage
// that weights every channel, 0 where the decal does not land. Every pixel evaluates surface(), also those it
// discards afterwards, so that the derivatives of its quad stay defined.
SurfaceOutput evaluateDecal(DecalVaryings input, out SurfaceInput surfaceIn, out float weight)
{
    const DecalRecord record = g_decals[input.decal];
    const int2 pixel = int2(input.position.xy);
    const float2 centre = float2(pixel) + 0.5;
    const float depth = depthAt(pixel);
    const float3 position = worldAt(centre, depth);
    const float3 local = transformPoint(record.worldToDecal, position);

    // The receiver normal from the depth of the four neighbours: on each axis the side closer in depth, so that
    // silhouettes do not bend it. Screen y points down, so dy x dx faces the camera.
    const float left = depthAt(pixel - int2(1, 0));
    const float right = depthAt(pixel + int2(1, 0));
    const float up = depthAt(pixel - int2(0, 1));
    const float down = depthAt(pixel + int2(0, 1));
    const float3 dx = abs(right - depth) < abs(depth - left) ? worldAt(centre + float2(1.0, 0.0), right) - position
                                                             : position - worldAt(centre - float2(1.0, 0.0), left);
    const float3 dy = abs(down - depth) < abs(depth - up) ? worldAt(centre + float2(0.0, 1.0), down) - position
                                                          : position - worldAt(centre - float2(0.0, 1.0), up);
    const float3 toCamera = g_view.effects.z > 0.5 ? -g_view.cameraForward.xyz
                                                   : normalize(g_view.cameraPosition.xyz - position);
    const float3 crossed = cross(dy, dx);
    const float3 normal = dot(crossed, crossed) > 1e-20 ? normalize(crossed) : toCamera;

    // The receiver's tangent frame from the box's x and y axes.
    const float3 axisX = normalize(boxAxis(record.decalToWorld, 0u));
    const float3 axisY = normalize(boxAxis(record.decalToWorld, 1u));
    const float3 axisZ = normalize(boxAxis(record.decalToWorld, 2u));
    const float3 tangent = axisX - normal * dot(normal, axisX);
    const float3 bitangent = axisY - normal * dot(normal, axisY);

    s_decalUv = float2(local.x + 0.5, 0.5 - local.y);
    s_decalUvDx = decalUv(record, planeHit(centre + float2(1.0, 0.0), depth, position, normal)) - s_decalUv;
    s_decalUvDy = decalUv(record, planeHit(centre + float2(0.0, 1.0), depth, position, normal)) - s_decalUv;

    surfaceIn.worldPosition = position;
    surfaceIn.normal = normal;
    surfaceIn.tangent = dot(tangent, tangent) > 1e-12 ? normalize(tangent) : axisX;
    surfaceIn.bitangent = dot(bitangent, bitangent) > 1e-12 ? normalize(bitangent) : axisY;
    surfaceIn.uv = s_decalUv;
    surfaceIn.color = record.color;
    surfaceIn.viewDirection = toCamera;
    surfaceIn.time = g_view.windGusts.w;
    surfaceIn.customData = float4(record.state.x, record.state.y, record.state.z, float(record.info.z));
    surfaceIn.frontFace = true;
    surfaceIn.material = record.info.y;
    const SurfaceOutput output = surface(surfaceIn);

    const bool inside = depth > 0.0 && all(abs(local) <= 0.5);
    const bool receives = (g_receiverMask.Load(int3(pixel, 0)) & record.info.x) != 0u;
    // 1 up to angleFadeStart between the receiver normal and the box's +z axis, 0 from angleFadeEnd.
    const float angleFade =
        saturate((dot(normal, axisZ) - record.fades.y) / max(record.fades.x - record.fades.y, 1e-5));
    // To 0 over the last depthFade of the half depth towards the box's z faces.
    const float depthFade = record.fades.z > 0.0 ? saturate((1.0 - 2.0 * abs(local.z)) / record.fades.z) : 1.0;
    // To 0 over the last 10 % of drawDistance from the camera to the box's centre.
    const float3 boxCentre = float3(record.decalToWorld[0].w, record.decalToWorld[1].w, record.decalToWorld[2].w);
    const float drawDistance = record.fades.w;
    const float distanceFade =
        drawDistance > 0.0
            ? saturate((drawDistance - length(boxCentre - g_view.cameraPosition.xyz)) / (0.1 * drawDistance))
            : 1.0;
    weight = inside && receives ? saturate(output.opacity) * saturate(record.state.w) * saturate(record.state.z) *
                                      angleFade * depthFade * distanceFade
                                : 0.0;
    return output;
}

#if defined(ILLUSION_PASS_DECAL)

// The decal buffer: each channel's value with the coverage in alpha; the blend state premultiplies it and keeps the
// remaining weight of the decals below in alpha.
struct DecalTargets
{
    float4 baseColor : SV_Target0;
    float4 normal : SV_Target1; // world-space normal as n / 2 + 1/2
    float4 orm : SV_Target2;    // occlusion, roughness, metallic
};

[shader("fragment")]
DecalTargets fragmentMain(DecalVaryings input)
{
    SurfaceInput surfaceIn;
    float weight;
    const SurfaceOutput output = evaluateDecal(input, surfaceIn, weight);
    if (weight <= 0.0)
    {
        discard;
    }
    DecalTargets targets;
    targets.baseColor = float4(saturate(output.baseColor), weight);
    float3 normal = surfaceIn.normal;
    if ((kDecalChannels & kDecalNormalChannel) != 0u)
    {
        normal = normalize(surfaceIn.tangent * output.normal.x + surfaceIn.bitangent * output.normal.y +
                           surfaceIn.normal * output.normal.z);
    }
    targets.normal = float4(normal * 0.5 + 0.5, weight);
    targets.orm = float4(saturate(output.occlusion), saturate(output.roughness), saturate(output.metallic), weight);
    return targets;
}

#elif defined(ILLUSION_PASS_DECAL_EMISSION)

// Added to the HDR colour (blend One, One).
[shader("fragment")]
float4 fragmentMain(DecalVaryings input) : SV_Target
{
    SurfaceInput surfaceIn;
    float weight;
    const SurfaceOutput output = evaluateDecal(input, surfaceIn, weight);
    if (weight <= 0.0)
    {
        discard;
    }
    return float4(max(output.emission, 0.0) * weight, 0.0);
}

#else
#error "a decal pass needs ILLUSION_PASS_DECAL or ILLUSION_PASS_DECAL_EMISSION"
#endif
