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
TextureCube g_specularCube : register(t1, space0);
Texture2D g_brdfLut : register(t2, space0);
Texture2D<float> g_ambientOcclusion : register(t5, space0);
SamplerState g_clampSampler : register(s0, space0);
[[vk::push_constant]] ConstantBuffer<PushConstants> g_push;

#include "ShadowReceiver.slang"

ConstantBuffer<MaterialConstants> g_material : register(b0, space1);
Texture2D g_baseColorTexture : register(t0, space1);
Texture2D g_metallicRoughnessTexture : register(t1, space1);
Texture2D g_normalTexture : register(t2, space1);
Texture2D g_occlusionTexture : register(t3, space1);
Texture2D g_emissiveTexture : register(t4, space1);
SamplerState g_sampler : register(s0, space1);

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
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instanceId: SV_InstanceID)
{
    const InstanceData instance = g_instances[g_push.instanceOffset + instanceId];
    const float4 world = mul(instance.world, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(g_view.viewProjection, world);
    output.worldPosition = world.xyz;
    output.normal = mul((float3x3)instance.normalMatrix, input.normal);
    output.tangent = float4(mul((float3x3)instance.world, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    return output;
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
    float3 sampled = g_normalTexture.Sample(g_sampler, input.uv).xyz * 2.0 - 1.0;
    sampled.xy *= g_material.emissiveFactor.w;
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
    const float4 baseColor = g_baseColorTexture.Sample(g_sampler, input.uv) * g_material.baseColorFactor;
    if (g_material.alphaCutoff >= 0.0 && baseColor.a < g_material.alphaCutoff)
    {
        discard;
    }
    const float4 metallicRoughness = g_metallicRoughnessTexture.Sample(g_sampler, input.uv);
    const float metallic = saturate(metallicRoughness.b * g_material.metallicFactor);
    const float roughness = clamp(metallicRoughness.g * g_material.roughnessFactor, 0.045, 1.0);
    const float occlusion = lerp(1.0, g_occlusionTexture.Sample(g_sampler, input.uv).r, g_material.occlusionStrength);
    const float3 emissive = g_emissiveTexture.Sample(g_sampler, input.uv).rgb * g_material.emissiveFactor.rgb;

    Surface surface;
    surface.albedo = baseColor.rgb * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    surface.alpha = roughness * roughness;
    surface.normal = surfaceNormal(input, frontFace);
    surface.view = normalize(g_view.cameraPosition.xyz - input.worldPosition);

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

    return float4(color, baseColor.a);
}

// Depth prepass for alpha-masked materials (opaque ones use no fragment shader).
[shader("fragment")]
void prepassMaskedMain(VertexOutput input)
{
    const float alpha = g_baseColorTexture.Sample(g_sampler, input.uv).a * g_material.baseColorFactor.a;
    if (alpha < g_material.alphaCutoff)
    {
        discard;
    }
}
