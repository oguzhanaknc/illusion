// Forward PBR shading (glTF metallic-roughness): Cook-Torrance GGX specular with height-correlated
// Smith visibility and Schlick Fresnel, Lambert diffuse, directional and point lights, image-based
// lighting (SH irradiance + prefiltered specular cube map + split-sum BRDF table with multiple-scattering
// compensation), screen-space ambient occlusion on indirect light, cascaded PCSS shadows for one
// directional light and cube-map PCSS shadows for up to four point lights. Writes linear HDR radiance; exposure and tonemapping happen in Tonemap.slang.
// Must match the C++ layouts in SceneRenderer.cpp and RenderResources.cpp.

#include "ForwardBindings.slang"

struct MaterialConstants
{
    float4 baseColorFactor;
    float4 emissiveFactor;  // rgb; w: normal scale
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float alphaCutoff;      // < 0 when alpha masking is off
    float4 wind;            // x: sway, y: flutter
    float4 transmission;    // rgb: diffuse transmission * its colour, w: diffuse transmission
};

// Material slots, used by sampleMaterial().
static const uint kBaseColor = 0;
static const uint kMetallicRoughness = 1;
static const uint kNormal = 2;
static const uint kOcclusion = 3;
static const uint kEmissive = 4;

#if defined(ILLUSION_BINDLESS)
// Bindless (ForwardPbrBindless.slang): every material is a record in one table, textures come from one
// descriptor array, and each instance names its material.
struct MaterialRecord
{
    MaterialConstants constants;
    uint textures[5]; // indices into g_textures, by slot
    uint samplerIndex;
    uint2 padding;
};
StructuredBuffer<MaterialRecord> g_materials : register(t7, space0);
SamplerState g_materialSamplers[9] : register(s2, space0);
Texture2D g_textures[] : register(t0, space1);

MaterialConstants materialConstants(uint material)
{
    return g_materials[material].constants;
}

float4 sampleMaterial(uint material, uint slot, float2 uv)
{
    const MaterialRecord record = g_materials[material];
    // The material varies per instance inside one draw, hence the non-uniform indexing.
    return g_textures[NonUniformResourceIndex(record.textures[slot])].Sample(
        g_materialSamplers[NonUniformResourceIndex(record.samplerIndex)], uv);
}
#else
// One binding set per material (set 1).
ConstantBuffer<MaterialConstants> g_material : register(b0, space1);
Texture2D g_baseColorTexture : register(t0, space1);
Texture2D g_metallicRoughnessTexture : register(t1, space1);
Texture2D g_normalTexture : register(t2, space1);
Texture2D g_occlusionTexture : register(t3, space1);
Texture2D g_emissiveTexture : register(t4, space1);
SamplerState g_sampler : register(s0, space1);

MaterialConstants materialConstants(uint material)
{
    return g_material;
}

float4 sampleMaterial(uint material, uint slot, float2 uv)
{
    switch (slot)
    {
    case kBaseColor: return g_baseColorTexture.Sample(g_sampler, uv);
    case kMetallicRoughness: return g_metallicRoughnessTexture.Sample(g_sampler, uv);
    case kNormal: return g_normalTexture.Sample(g_sampler, uv);
    case kOcclusion: return g_occlusionTexture.Sample(g_sampler, uv);
    default: return g_emissiveTexture.Sample(g_sampler, uv);
    }
}
#endif

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION1;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 currentClip : TEXCOORD1;  // unjittered, for motion vectors
    float4 previousClip : TEXCOORD2; // previous frame
    nointerpolation uint2 entity : ENTITY;
    nointerpolation uint material : MATERIAL; // material table index (bindless)
    nointerpolation uint lod : LOD;           // InstanceData::extra.w: level of detail and cross-fade
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint vertexId: SV_VertexID, uint instanceId: SV_InstanceID,
                        uint firstInstance: SV_StartInstanceLocation)
{
    // Multi-draw indirect passes each draw's draw-list offset as its first instance; single draws pass
    // it as a push constant.
    const InstanceData instance = g_instances[g_drawList[g_push.instanceOffset + firstInstance + instanceId]];
    const float2 response = materialConstants(instance.extra.x).wind.xy;
    float4 world = mul(instance.world, float4(input.position, 1.0));
    // Skinned draws bind their vertices at an offset and start at vertex 0, so the vertex ID is the vertex's
    // index in the instance's range.
    const float3 previousPosition = (g_push.flags & kForwardSkinned) != 0u
                                        ? g_previousPositions[g_push.previousVertex + vertexId].xyz
                                        : input.position;
    float4 previousWorld = mul(instance.previousWorld, float4(previousPosition, 1.0));
    // The prepass runs this shader too, so depth, IDs and motion vectors move with the wind.
    const float3 origin = mul(instance.world, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const float3 previousOrigin = mul(instance.previousWorld, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    world.xyz += windOffset(g_view.wind, g_view.windGusts, g_view.windGusts.w, response, origin, world.xyz);
    previousWorld.xyz += windOffset(g_view.wind, g_view.windGusts, g_view.windHistory.x, response, previousOrigin,
                                    previousWorld.xyz);
    VertexOutput output;
    output.position = mul(g_view.viewProjection, world);
    output.worldPosition = world.xyz;
    output.normal = mul((float3x3)instance.normalMatrix, input.normal);
    output.tangent = float4(mul((float3x3)instance.world, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    output.material = instance.extra.x;
    output.entity = instance.extra.yz;
    output.lod = instance.extra.w;
    output.currentClip = mul(g_view.unjitteredViewProjection, world);
    output.previousClip = mul(g_view.previousViewProjection, previousWorld);
    return output;
}

// Screen-space motion from the previous frame to this one, in UV units (origin top-left).
float2 motionVector(VertexOutput input)
{
    const float2 current = input.currentClip.xy / input.currentClip.w;
    const float2 previous = input.previousClip.xy / input.previousClip.w;
    return (current - previous) * float2(0.5, -0.5);
}

#include "ForwardLighting.slang"

// Normal from the normal map; without vertex tangents the tangent frame comes from screen-space
// derivatives of position and UV.
float3 surfaceNormal(VertexOutput input, bool frontFace)
{
    float3 n = normalize(input.normal);
    if (!frontFace)
    {
        n = -n;
    }
    const float3 sampled = decodeNormalMap(sampleMaterial(input.material, kNormal, input.uv).xy,
                                           materialConstants(input.material).emissiveFactor.w);
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
    return normalize(t * sampled.x + b * sampled.y + n * sampled.z);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace) : SV_Target
{
    const MaterialConstants material = materialConstants(input.material);
    const float4 baseColor = sampleMaterial(input.material, kBaseColor, input.uv) * material.baseColorFactor;
    if ((material.alphaCutoff >= 0.0 && baseColor.a < material.alphaCutoff) ||
        lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
    const float4 metallicRoughness = sampleMaterial(input.material, kMetallicRoughness, input.uv);
    const float metallic = saturate(metallicRoughness.b * material.metallicFactor);
    const float roughness = clamp(metallicRoughness.g * material.roughnessFactor, 0.045, 1.0);
    const float occlusion =
        lerp(1.0, sampleMaterial(input.material, kOcclusion, input.uv).r, material.occlusionStrength);
    const float3 emissive = sampleMaterial(input.material, kEmissive, input.uv).rgb * material.emissiveFactor.rgb;

    Surface surface;
    surface.albedo = baseColor.rgb * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    surface.alpha = roughness * roughness;
    surface.normal = surfaceNormal(input, frontFace);
    surface.view = g_view.effects.z > 0.5 ? -g_view.cameraForward.xyz : normalize(g_view.cameraPosition.xyz - input.worldPosition);
    surface.transmission = material.transmission;
    const bool translucent = material.transmission.w > 0.0;
    float3 geometricNormal = normalize(input.normal);
    if (!frontFace)
    {
        geometricNormal = -geometricNormal;
    }

    float ambientOcclusion = occlusion;
    if ((g_push.flags & 1u) != 0u && g_view.effects.x > 0.5)
    {
        ambientOcclusion *= g_ambientOcclusion.Load(int3(int2(input.position.xy), 0));
    }
    float3 color = environmentLighting(surface, roughness, ambientOcclusion);
    const uint shadowedLight = uint(g_view.shadow.y);
    for (uint i = 0; i < g_view.directionalCount; ++i)
    {
        const DirectionalLightData light = g_view.directional[i];
        float visibility = 1.0;
        if (i == shadowedLight)
        {
            visibility = shadowVisibility(input.worldPosition,
                                          receiverNormal(geometricNormal, -light.direction.xyz, translucent),
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
                                                 receiverNormal(geometricNormal, toLight, translucent),
                                                 input.position.xy);
        }
        color += shade(surface, toLight * rsqrt(distanceSquared), light.color.rgb * attenuation);
    }
    color += emissive;

    switch (uint(g_view.effects.y))
    {
    case 1: color = baseColor.rgb + emissive; break;
    case 2: color = baseColor.rgb; break;
    case 3: color = surface.normal * 0.5 + 0.5; break;
    case 4: color = roughness.xxx; break;
    case 5: color = metallic.xxx; break;
    case 6: color = ambientOcclusion.xxx; break;
    case 7:
    {
        const float distance = dot(input.worldPosition - g_view.cameraPosition.xyz, g_view.cameraForward.xyz);
        const float3 colors[4] = {float3(1,0.2,0.2),float3(0.2,1,0.2),float3(0.2,0.2,1),float3(1,1,0.2)};
        color = float3(0.2,0.2,0.2);
        for (uint c = 0; c < uint(g_view.shadow.x); ++c)
        {
            if (distance <= g_view.cascades[c].params.x) { color = colors[c]; break; }
        }
        break;
    }
    case 8: color = float3(motionVector(input) * 8.0 + 0.5, 0.5); break;
    case 9:
    {
        const float3 levels[4] = {float3(0.2, 0.9, 0.2), float3(0.95, 0.9, 0.2), float3(1.0, 0.55, 0.1),
                                  float3(0.95, 0.15, 0.15)};
        color = levels[min((input.lod >> kLodFadeLevelShift) & 0xFu, 3u)] * (0.35 + 0.65 * baseColor.rgb);
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
    return float4(color, baseColor.a);
}

// Depth prepass: depth plus motion vectors for TAA.
[shader("fragment")]
float2 prepassMain(VertexOutput input) : SV_Target
{
    return motionVector(input);
}

// Depth prepass for alpha-masked materials.
[shader("fragment")]
float2 prepassMaskedMain(VertexOutput input) : SV_Target
{
    const MaterialConstants material = materialConstants(input.material);
    const float alpha = sampleMaterial(input.material, kBaseColor, input.uv).a * material.baseColorFactor.a;
    if (alpha < material.alphaCutoff || lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
    return motionVector(input);
}

// Optional Entity-ID outputs; the normal prepass keeps its single attachment when IDs are disabled.
struct PrepassIds
{
    float2 motion : SV_Target0;
    uint2 entity : SV_Target1;
};

[shader("fragment")]
PrepassIds prepassIdsMain(VertexOutput input)
{
    PrepassIds output;
    output.motion = motionVector(input);
    output.entity = input.entity;
    return output;
}

[shader("fragment")]
PrepassIds prepassMaskedIdsMain(VertexOutput input)
{
    const MaterialConstants material = materialConstants(input.material);
    const float alpha = sampleMaterial(input.material, kBaseColor, input.uv).a * material.baseColorFactor.a;
    if (alpha < material.alphaCutoff || lodFadeDiscard(input.lod, input.position.xy)) { discard; }
    return prepassIdsMain(input);
}

// Transparent picking treats any nonzero coverage as pickable, using a private depth copy.
[shader("fragment")]
uint2 entityIdMain(VertexOutput input) : SV_Target0
{
    const MaterialConstants material = materialConstants(input.material);
    if (sampleMaterial(input.material, kBaseColor, input.uv).a * material.baseColorFactor.a <= 0.0 ||
        lodFadeDiscard(input.lod, input.position.xy))
    {
        discard;
    }
    return input.entity;
}

[shader("fragment")]
float4 wireframeMain(VertexOutput input) : SV_Target0
{
    const MaterialConstants material = materialConstants(input.material);
    const float alpha = sampleMaterial(input.material, kBaseColor, input.uv).a * material.baseColorFactor.a;
    if (alpha <= 0.0 || (material.alphaCutoff >= 0.0 && alpha < material.alphaCutoff)) { discard; }
    return float4(0.05, 0.8, 0.7, 1.0);
}
