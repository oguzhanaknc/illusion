// Forward PBR shading (glTF metallic-roughness): Cook-Torrance GGX specular with height-correlated
// Smith visibility and Schlick Fresnel, Lambert diffuse, directional and point lights, image-based
// lighting (SH irradiance + prefiltered specular cube map + split-sum BRDF table with multiple-scattering
// compensation), screen-space ambient occlusion on indirect light and cascaded PCSS shadows for one
// directional light. Writes linear HDR radiance; exposure and tonemapping happen in Tonemap.slang.
// Must match the C++ layouts in SceneRenderer.cpp and RenderResources.cpp.

#include "Common.slang"

struct PushConstants
{
    uint instanceOffset;
    uint flags; // bit 0: apply screen-space ambient occlusion (off for blended surfaces)
};

struct MaterialConstants
{
    float4 baseColorFactor;
    float4 emissiveFactor;  // rgb; w: normal scale
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float alphaCutoff;      // < 0 when alpha masking is off
};

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
StructuredBuffer<InstanceData> g_instances : register(t0, space0);
// Instance indices per draw: written by GPU culling for opaque batches, by the CPU for blended items.
StructuredBuffer<uint> g_drawList : register(t6, space0);
TextureCube g_specularCube : register(t1, space0);
Texture2D g_brdfLut : register(t2, space0);
Texture2D<float> g_ambientOcclusion : register(t5, space0);
SamplerState g_clampSampler : register(s0, space0);
[[vk::push_constant]] ConstantBuffer<PushConstants> g_push;

#include "ShadowReceiver.slang"

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
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instanceId: SV_InstanceID, uint firstInstance: SV_StartInstanceLocation)
{
    // Multi-draw indirect passes each draw's draw-list offset as its first instance; single draws pass
    // it as a push constant.
    const InstanceData instance = g_instances[g_drawList[g_push.instanceOffset + firstInstance + instanceId]];
    const float4 world = mul(instance.world, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(g_view.viewProjection, world);
    output.worldPosition = world.xyz;
    output.normal = mul((float3x3)instance.normalMatrix, input.normal);
    output.tangent = float4(mul((float3x3)instance.world, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    output.material = instance.extra.x;
    output.entity = instance.extra.yz;
    output.currentClip = mul(g_view.unjitteredViewProjection, world);
    output.previousClip =
        mul(g_view.previousViewProjection, mul(instance.previousWorld, float4(input.position, 1.0)));
    return output;
}

// Screen-space motion from the previous frame to this one, in UV units (origin top-left).
float2 motionVector(VertexOutput input)
{
    const float2 current = input.currentClip.xy / input.currentClip.w;
    const float2 previous = input.previousClip.xy / input.previousClip.w;
    return (current - previous) * float2(0.5, -0.5);
}

float distributionGgx(float nDotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float visibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
{
    const float a2 = alpha * alpha;
    const float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    const float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

float3 fresnelSchlick(float3 f0, float vDotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

struct Surface
{
    float3 albedo;
    float3 f0;
    float alpha; // roughness squared
    float3 normal;
    float3 view;
};

float3 shade(Surface surface, float3 lightDirection, float3 radiance)
{
    const float3 l = lightDirection;
    const float3 h = normalize(surface.view + l);
    const float nDotL = saturate(dot(surface.normal, l));
    const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
    const float nDotH = saturate(dot(surface.normal, h));
    const float vDotH = saturate(dot(surface.view, h));
    const float3 fresnel = fresnelSchlick(surface.f0, vDotH);
    const float3 specular = distributionGgx(nDotH, surface.alpha) *
                            visibilitySmithGgxCorrelated(nDotV, nDotL, surface.alpha) * fresnel;
    const float3 diffuse = (1.0 - fresnel) * surface.albedo / kPi;
    return (diffuse + specular) * radiance * nDotL;
}

// Normal from the normal map; without vertex tangents the tangent frame comes from screen-space
// derivatives of position and UV.
float3 surfaceNormal(VertexOutput input, bool frontFace)
{
    float3 n = normalize(input.normal);
    if (!frontFace)
    {
        n = -n;
    }
    float3 sampled = sampleMaterial(input.material, kNormal, input.uv).xyz * 2.0 - 1.0;
    sampled.xy *= materialConstants(input.material).emissiveFactor.w;
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

// Image-based lighting (Fdez-Aguera 2019 multiple-scattering split sum). `occlusion` darkens diffuse
// light directly and specular light through the specular-occlusion fit of Lagarde and de Rousiers 2014.
float3 environmentLighting(Surface surface, float roughness, float occlusion)
{
    const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
    const float2 brdf = g_brdfLut.SampleLevel(g_clampSampler, float2(nDotV, roughness), 0.0).xy;
    const float3 singleScatter = surface.f0 * brdf.x + brdf.y;
    const float multiScatterEnergy = 1.0 - (brdf.x + brdf.y);
    const float3 averageFresnel = surface.f0 + (1.0 - surface.f0) / 21.0;
    const float3 multiScatter = singleScatter * averageFresnel / (1.0 - multiScatterEnergy * averageFresnel);
    const float3 diffuseWeight = surface.albedo * (1.0 - singleScatter - multiScatter * multiScatterEnergy);

    const float3x3 toEnvironment = (float3x3)g_view.skyRotation;
    const float3 irradiance = shIrradiance(g_view.sh, mul(toEnvironment, surface.normal)) / kPi;
    const float3 reflected = reflect(-surface.view, surface.normal);
    const float3 radiance =
        g_specularCube.SampleLevel(g_clampSampler, mul(toEnvironment, reflected), roughness * g_view.sky.y).rgb *
        g_view.sky.x;
    const float specularOcclusion =
        saturate(pow(nDotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion);
    return singleScatter * radiance * specularOcclusion +
           (multiScatter * multiScatterEnergy + diffuseWeight) * irradiance * occlusion;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace) : SV_Target
{
    const MaterialConstants material = materialConstants(input.material);
    const float4 baseColor = sampleMaterial(input.material, kBaseColor, input.uv) * material.baseColorFactor;
    if (material.alphaCutoff >= 0.0 && baseColor.a < material.alphaCutoff)
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
            float3 geometricNormal = normalize(input.normal);
            if (!frontFace)
            {
                geometricNormal = -geometricNormal;
            }
            visibility = shadowVisibility(input.worldPosition, geometricNormal, input.position.xy);
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
        const float attenuation = window * window / distanceSquared;
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
    if (alpha < material.alphaCutoff)
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
    if (alpha < material.alphaCutoff) { discard; }
    return prepassIdsMain(input);
}

// Transparent picking treats any nonzero coverage as pickable, using a private depth copy.
[shader("fragment")]
uint2 entityIdMain(VertexOutput input) : SV_Target0
{
    const MaterialConstants material = materialConstants(input.material);
    if (sampleMaterial(input.material, kBaseColor, input.uv).a * material.baseColorFactor.a <= 0.0) { discard; }
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
