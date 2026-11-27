// The passes of material shaders (ADR 0063). ShaderCompiler compiles illusion/Surface.slang, the `.shader`
// source and this file once per variant, with the entry points vertexMain and fragmentMain and the defines
//   ILLUSION_PASS_FORWARD, ILLUSION_PASS_FORWARD_BINDLESS, ILLUSION_PASS_DEPTH_PREPASS,
//   ILLUSION_PASS_SHADOW_CASTER, ILLUSION_PASS_MOTION_VECTORS or ILLUSION_PASS_ENTITY_IDS: the pass;
//   ILLUSION_ALPHA_MASK or ILLUSION_ALPHA_BLEND: the alpha mode (neither: opaque);
//   ILLUSION_WORLD_OFFSET: the shader has worldOffset();
//   ILLUSION_MATERIAL_PARAMETERS: Parameters has fields;
//   ILLUSION_PARAMETER_PADDING: 32-bit words that pad Parameters to 256 bytes in a structured buffer;
//   ILLUSION_SHADING_MODEL: the ShadingModel value (ShaderAsset.hpp).
// Vertex input is the engine's Vertex plus the colour stream (MaterialVaryings.slang); the space-0 bindings are
// those of ForwardBindings.slang or, for shadow casters, the shadow pass's. Material bindings: bound passes take
// the parameter block at b0 and the textures at t0..t7 of space 1 with one sampler at s0; bindless passes read
// the material's record from t11 of space 0 by the instance's material index and the textures from the
// descriptor array of space 1. SceneRenderer.cpp and RenderResources.cpp must match.

#include "Surface.slang"
#include "MaterialVaryings.slang"

static const uint kMaxMaterialTextures = 8;

#if defined(ILLUSION_PASS_SHADOW_CASTER)

struct ShadowPush
{
    uint instanceOffset;
    uint cascade; // shadow view: a cascade, or kMaxShadowCascades + 6 * point shadow slot + face
};

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
StructuredBuffer<InstanceData> g_instances : register(t0, space0);
[[vk::push_constant]] ConstantBuffer<ShadowPush> g_push;

#else

#include "../ForwardLighting.slang"

#endif

#if defined(ILLUSION_PASS_FORWARD_BINDLESS)

#if !defined(ILLUSION_PARAMETER_PADDING)
#error "the bindless pass needs ILLUSION_PARAMETER_PADDING"
#endif

// One record of 272 bytes per material: its sampler, then its parameters padded to 256 bytes.
struct MaterialRecord
{
    uint4 header; // x: index into g_materialSamplers (wrap modes)
#if defined(ILLUSION_MATERIAL_PARAMETERS)
    Parameters parameters;
#endif
#if ILLUSION_PARAMETER_PADDING > 0
    uint padding[ILLUSION_PARAMETER_PADDING];
#endif
};

StructuredBuffer<MaterialRecord> g_materialTable : register(t11, space0);
SamplerState g_materialSamplers[9] : register(s2, space0);
Texture2D g_textures[] : register(t0, space1);

// The material varies per instance inside one multi-draw, hence the non-uniform indexing.
float4 illusionSampleTexture(uint material, Texture2DSlot texture, float2 uv)
{
    const uint samplerIndex = g_materialTable[material].header.x;
    return g_textures[NonUniformResourceIndex(texture)].Sample(g_materialSamplers[NonUniformResourceIndex(samplerIndex)],
                                                               uv);
}

float4 illusionSampleTextureLevel(uint material, Texture2DSlot texture, float2 uv, float level)
{
    const uint samplerIndex = g_materialTable[material].header.x;
    return g_textures[NonUniformResourceIndex(texture)].SampleLevel(
        g_materialSamplers[NonUniformResourceIndex(samplerIndex)], uv, level);
}

#else

#if defined(ILLUSION_MATERIAL_PARAMETERS)
ConstantBuffer<Parameters> g_materialParameters : register(b0, space1);
#endif
Texture2D g_materialTextures[kMaxMaterialTextures] : register(t0, space1);
SamplerState g_materialSampler : register(s0, space1);

float4 illusionSampleTexture(uint material, Texture2DSlot texture, float2 uv)
{
    return g_materialTextures[min(texture, kMaxMaterialTextures - 1)].Sample(g_materialSampler, uv);
}

float4 illusionSampleTextureLevel(uint material, Texture2DSlot texture, float2 uv, float level)
{
    return g_materialTextures[min(texture, kMaxMaterialTextures - 1)].SampleLevel(g_materialSampler, uv, level);
}

#endif

Parameters illusionMaterialParameters(uint material)
{
#if defined(ILLUSION_MATERIAL_PARAMETERS) && defined(ILLUSION_PASS_FORWARD_BINDLESS)
    return g_materialTable[material].parameters;
#elif defined(ILLUSION_MATERIAL_PARAMETERS)
    return g_materialParameters;
#else
    Parameters none;
    return none;
#endif
}

#if defined(ILLUSION_WORLD_OFFSET)
float3 materialOffset(InstanceData instance, float3 origin, float3 world, float3 normal, float2 uv, float time)
{
    WorldOffsetInput input;
    input.worldPosition = world;
    input.objectOrigin = origin;
    input.normal = normal;
    input.uv = uv;
    input.time = time;
    input.customData = instanceCustomData(instance);
    input.material = instance.extra.x;
    return worldOffset(input);
}
#endif

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint vertexId: SV_VertexID, uint instanceId: SV_InstanceID,
                        uint firstInstance: SV_StartInstanceLocation)
{
#if defined(ILLUSION_PASS_SHADOW_CASTER)
    const InstanceData instance = g_instances[g_push.instanceOffset + instanceId];
    const float3 previousPosition = input.position;
#else
    // Multi-draw indirect passes each draw's draw-list offset as its first instance; single draws pass it as a
    // push constant. Skinned draws bind their vertices at an offset and start at vertex 0, so the vertex ID is
    // the vertex's index in the instance's range.
    const InstanceData instance = g_instances[g_drawList[g_push.instanceOffset + firstInstance + instanceId]];
    const float3 previousPosition = (g_push.flags & kForwardSkinned) != 0u
                                        ? g_previousPositions[g_push.previousVertex + vertexId].xyz
                                        : input.position;
#endif
    float4 world = mul(instance.world, float4(input.position, 1.0));
    float4 previousWorld = mul(instance.previousWorld, float4(previousPosition, 1.0));
    const float3 normal = mul((float3x3)instance.normalMatrix, input.normal);
#if defined(ILLUSION_WORLD_OFFSET)
    // Every pass moves the vertices alike, so depth, shadows and motion vectors agree with the colour.
    const float3 origin = mul(instance.world, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float3 previousOrigin = mul(instance.previousWorld, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    world.xyz += materialOffset(instance, origin, world.xyz, normal, input.uv, g_view.windGusts.w);
    previousWorld.xyz +=
        materialOffset(instance, previousOrigin, previousWorld.xyz, normal, input.uv, g_view.windHistory.x);
#endif
    VertexOutput output;
#if defined(ILLUSION_PASS_SHADOW_CASTER)
    if (g_push.cascade < kMaxShadowCascades)
    {
        output.position = mul(g_view.cascades[g_push.cascade].viewProjection, world);
    }
    else
    {
        const uint face = g_push.cascade - kMaxShadowCascades;
        output.position = mul(g_view.pointShadows[face / kPointShadowFaces].faces[face % kPointShadowFaces], world);
    }
#else
    output.position = mul(g_view.viewProjection, world);
#endif
    output.worldPosition = world.xyz;
    output.normal = normal;
    output.tangent = float4(mul((float3x3)instance.world, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    output.color = input.color;
    output.customData = instanceCustomData(instance);
    output.material = instance.extra.x;
    output.entity = instance.extra.yz;
    output.lod = instance.extra.w;
    output.currentClip = mul(g_view.unjitteredViewProjection, world);
    output.previousClip = mul(g_view.previousViewProjection, previousWorld);
    return output;
}

// The surface function's input for one pixel; tangents come from screen-space derivatives without vertex
// tangents.
SurfaceInput surfaceInput(VertexOutput input, bool frontFace)
{
    float3 n = normalize(input.normal);
    if (!frontFace)
    {
        n = -n;
    }
    float3 t;
    float3 b;
    if (input.tangent.w != 0.0)
    {
        t = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
        b = cross(n, t) * input.tangent.w;
    }
    else
    {
        const float3 dp1 = ddx(input.worldPosition);
        const float3 dp2 = ddy(input.worldPosition);
        const float2 duv1 = ddx(input.uv);
        const float2 duv2 = ddy(input.uv);
        const float3 dp2perp = cross(dp2, n);
        const float3 dp1perp = cross(n, dp1);
        t = dp2perp * duv1.x + dp1perp * duv2.x;
        b = dp2perp * duv1.y + dp1perp * duv2.y;
        const float invMax = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
        t *= invMax;
        b *= -invMax; // UV origin is top-left
    }
    SurfaceInput surface;
    surface.worldPosition = input.worldPosition;
    surface.normal = n;
    surface.tangent = t;
    surface.bitangent = b;
    surface.uv = input.uv;
    surface.color = input.color;
    surface.viewDirection = g_view.effects.z > 0.5 ? -g_view.cameraForward.xyz
                                                   : normalize(g_view.cameraPosition.xyz - input.worldPosition);
    surface.time = g_view.windGusts.w;
    surface.customData = input.customData;
    surface.frontFace = frontFace;
    surface.material = input.material;
    return surface;
}

// True if the pixel is not drawn: cut by the alpha mask, or left to the other level of a cross-fade.
bool discarded(VertexOutput input, SurfaceOutput surface)
{
#if defined(ILLUSION_ALPHA_MASK)
    if (surface.opacity < surface.opacityMask)
    {
        return true;
    }
#endif
    return lodFadeDiscard(input.lod, input.position.xy);
}

#if defined(ILLUSION_PASS_FORWARD) || defined(ILLUSION_PASS_FORWARD_BINDLESS)

// Lit colour of @p output: image-based light, the frame's lights and shadows, then emission, a debug view
// (ViewConstants::effects.y) and, for blended surfaces, the fog.
float3 shadeSurface(VertexOutput input, SurfaceInput surfaceIn, SurfaceOutput output)
{
    const float metallic = saturate(output.metallic);
    const float roughness = clamp(output.roughness, 0.045, 1.0);
    const float3 baseColor = max(output.baseColor, 0.0);
    const float3 emission = max(output.emission, 0.0);
    Surface surface;
    surface.albedo = baseColor * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    surface.alpha = roughness * roughness;
    surface.normal = normalize(surfaceIn.tangent * output.normal.x + surfaceIn.bitangent * output.normal.y +
                               surfaceIn.normal * output.normal.z);
    surface.view = surfaceIn.viewDirection;
    // The thinner the surface, the more light passes to its other side (TwoSidedFoliage) or wraps around it
    // (Subsurface), tinted by the subsurface colour.
    const float transmission = saturate(1.0 - output.thickness);
    surface.transmission = float4(saturate(output.subsurfaceColor) * transmission, transmission);
    surface.scatter = float4(saturate(output.subsurfaceColor), transmission);
    surface.coat = float4(surfaceIn.normal, saturate(output.clearCoat));
    surface.coatRoughness = clamp(output.clearCoatRoughness, 0.045, 1.0);
    // Light reaches translucent surfaces from behind too, which moves their shadow test to the lit side.
    const bool translucent =
        (kShadingModel == kShadingModelTwoSidedFoliage || kShadingModel == kShadingModelSubsurface) &&
        transmission > 0.0;
    float ambientOcclusion = saturate(output.occlusion);
    if ((g_push.flags & 1u) != 0u && g_view.effects.x > 0.5)
    {
        ambientOcclusion *= g_ambientOcclusion.Load(int3(int2(input.position.xy), 0));
    }
    float3 color;
    if (kShadingModel == kShadingModelUnlit)
    {
        color = baseColor;
    }
    else
    {
        color = environmentLighting(surface, roughness, ambientOcclusion);
        const uint shadowedLight = uint(g_view.shadow.y);
        for (uint i = 0; i < g_view.directionalCount; ++i)
        {
            const DirectionalLightData light = g_view.directional[i];
            float visibility = 1.0;
            if (i == shadowedLight)
            {
                visibility = shadowVisibility(input.worldPosition,
                                              receiverNormal(surfaceIn.normal, -light.direction.xyz, translucent),
                                              input.position.xy);
            }
            color += shade(surface, -light.direction.xyz, light.color.rgb * visibility);
        }
        for (uint i = 0; i < g_view.pointCount; ++i)
        {
            const PointLightData light = g_view.points[i];
            const float3 toLight = light.positionRange.xyz - input.worldPosition;
            const float distanceSquared = max(dot(toLight, toLight), 1e-4);
            // Inverse-square falloff windowed to reach zero at the range (Karis 2013).
            const float ratio = distanceSquared / (light.positionRange.w * light.positionRange.w);
            const float window = saturate(1.0 - ratio * ratio);
            float attenuation = window * window / distanceSquared;
            const uint shadowSlot = uint(light.color.w);
            if (shadowSlot > 0u && attenuation > 0.0)
            {
                attenuation *= pointShadowVisibility(shadowSlot - 1u, input.worldPosition,
                                                     receiverNormal(surfaceIn.normal, toLight, translucent),
                                                     input.position.xy);
            }
            color += shade(surface, toLight * rsqrt(distanceSquared), light.color.rgb * attenuation);
        }
    }
    color += emission;

    switch (uint(g_view.effects.y))
    {
    case 1: color = baseColor + emission; break;
    case 2: color = baseColor; break;
    case 3: color = surface.normal * 0.5 + 0.5; break;
    case 4: color = roughness.xxx; break;
    case 5: color = metallic.xxx; break;
    case 6: color = ambientOcclusion.xxx; break;
    case 7:
    {
        const float distance = dot(input.worldPosition - g_view.cameraPosition.xyz, g_view.cameraForward.xyz);
        const float3 colors[4] = {float3(1, 0.2, 0.2), float3(0.2, 1, 0.2), float3(0.2, 0.2, 1), float3(1, 1, 0.2)};
        color = float3(0.2, 0.2, 0.2);
        for (uint c = 0; c < uint(g_view.shadow.x); ++c)
        {
            if (distance <= g_view.cascades[c].params.x)
            {
                color = colors[c];
                break;
            }
        }
        break;
    }
    case 8: color = float3(motionVector(input) * 8.0 + 0.5, 0.5); break;
    case 9:
    {
        const float3 levels[4] = {float3(0.2, 0.9, 0.2), float3(0.95, 0.9, 0.2), float3(1.0, 0.55, 0.1),
                                  float3(0.95, 0.15, 0.15)};
        color = levels[min((input.lod >> kLodFadeLevelShift) & 0xFu, 3u)] * (0.35 + 0.65 * baseColor);
        break;
    }
    }
    if ((g_push.flags & 2u) != 0u && uint(g_view.effects.y) == 0u)
    {
        const float viewDepth = dot(input.worldPosition - g_view.cameraPosition.xyz, g_view.cameraForward.xyz);
        const float4 fog =
            sampleFog(g_fogVolume, g_clampSampler, g_view.fog, input.position.xy * g_view.fogScreen.xy, viewDepth);
        color = color * fog.a + fog.rgb;
    }
    return color;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace) : SV_Target
{
    const SurfaceInput surfaceIn = surfaceInput(input, frontFace);
    const SurfaceOutput output = surface(surfaceIn);
    if (discarded(input, output))
    {
        discard;
    }
    const float3 color = shadeSurface(input, surfaceIn, output);
#if defined(ILLUSION_ALPHA_BLEND)
    return float4(color, saturate(output.opacity));
#else
    return float4(color, output.opacity);
#endif
}

#elif defined(ILLUSION_PASS_DEPTH_PREPASS) || defined(ILLUSION_PASS_SHADOW_CASTER)

[shader("fragment")]
void fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace)
{
#if defined(ILLUSION_ALPHA_MASK)
    if (discarded(input, surface(surfaceInput(input, frontFace))))
    {
        discard;
    }
#else
    if (lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
#endif
}

#elif defined(ILLUSION_PASS_MOTION_VECTORS)

[shader("fragment")]
float2 fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace) : SV_Target
{
#if defined(ILLUSION_ALPHA_MASK)
    if (discarded(input, surface(surfaceInput(input, frontFace))))
    {
        discard;
    }
#else
    if (lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
#endif
    return motionVector(input);
}

#elif defined(ILLUSION_PASS_ENTITY_IDS) && defined(ILLUSION_ALPHA_BLEND)

// Transparent picking treats any nonzero coverage as pickable.
[shader("fragment")]
uint2 fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace) : SV_Target0
{
    if (surface(surfaceInput(input, frontFace)).opacity <= 0.0 || lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
    return input.entity;
}

#elif defined(ILLUSION_PASS_ENTITY_IDS)

struct PrepassIds
{
    float2 motion : SV_Target0;
    uint2 entity : SV_Target1;
};

[shader("fragment")]
PrepassIds fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace)
{
#if defined(ILLUSION_ALPHA_MASK)
    if (discarded(input, surface(surfaceInput(input, frontFace))))
    {
        discard;
    }
#else
    if (lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
#endif
    PrepassIds output;
    output.motion = motionVector(input);
    output.entity = input.entity;
    return output;
}

#endif
