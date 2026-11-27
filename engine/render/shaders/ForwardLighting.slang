// Lighting of the forward passes of material shaders (illusion/MaterialPass.slang): Cook-Torrance GGX specular,
// Lambert diffuse and image-based lighting, varied by the shading model of the shader being compiled
// (ILLUSION_SHADING_MODEL, a ShadingModel value of ShaderAsset.hpp). The model is a constant of each shader's
// variants, so every branch on it is resolved at compile time and the Default model pays for none of the others.
// Needs the bindings of ForwardBindings.slang.

#pragma once

#include "ForwardBindings.slang"

#if !defined(ILLUSION_SHADING_MODEL)
#error "ForwardLighting.slang needs ILLUSION_SHADING_MODEL"
#endif

static const uint kShadingModelDefault = 0;
static const uint kShadingModelUnlit = 1;
static const uint kShadingModelSubsurface = 2;
static const uint kShadingModelTwoSidedFoliage = 3;
static const uint kShadingModelClearCoat = 4;
static const uint kShadingModel = ILLUSION_SHADING_MODEL;

// Reflectance of the clear coat at normal incidence (index of refraction 1.5).
static const float kClearCoatF0 = 0.04;

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
    // TwoSidedFoliage: rgb the transmitted tint, w the fraction of diffuse light transmitted.
    float4 transmission;
    // Subsurface: rgb the scatter colour, w the wrap (0: Lambert, 1: light reaches the whole sphere).
    float4 scatter;
    // ClearCoat: xyz the coat's normal (the geometric one), w its strength.
    float4 coat;
    float coatRoughness; // perceptual
};

// Direct lighting from one light, by the shading model:
// - TwoSidedFoliage (KHR_materials_diffuse_transmission) splits its diffuse light between this side and the
//   other: light that reaches the back face leaves this side as Lambertian light tinted by the transmission
//   colour.
// - Subsurface wraps its diffuse light past the terminator (Green 2004, "Real-Time Approximations to Subsurface
//   Scattering"): the light beyond Lambert's is tinted by the scatter colour.
// - ClearCoat adds a dielectric GGX lobe over the base, which loses the light the coat reflects.
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
    if (kShadingModel == kShadingModelTwoSidedFoliage)
    {
        const float3 reflected = (diffuse * (1.0 - surface.transmission.w) + specular) * nDotL;
        const float backDotL = saturate(-dot(surface.normal, l));
        const float3 transmitted = surface.transmission.rgb * (1.0 - surface.f0) * surface.albedo / kPi * backDotL;
        return (reflected + transmitted) * radiance;
    }
    if (kShadingModel == kShadingModelSubsurface)
    {
        const float wrap = surface.scatter.w;
        const float wrapped = saturate((dot(surface.normal, l) + wrap) / (1.0 + wrap));
        const float3 scattered = surface.scatter.rgb * (1.0 - surface.f0) * surface.albedo / kPi * (wrapped - nDotL);
        return ((diffuse + specular) * nDotL + scattered) * radiance;
    }
    if (kShadingModel == kShadingModelClearCoat)
    {
        const float3 n = surface.coat.xyz;
        const float coatAlpha = surface.coatRoughness * surface.coatRoughness;
        const float coatNDotL = saturate(dot(n, l));
        const float coatNDotV = max(dot(n, surface.view), 1e-4);
        const float coatFresnel = fresnelSchlick(kClearCoatF0.xxx, vDotH).x * surface.coat.w;
        const float coatSpecular = distributionGgx(saturate(dot(n, h)), coatAlpha) *
                                   visibilitySmithGgxCorrelated(coatNDotV, coatNDotL, coatAlpha) * coatFresnel;
        return ((diffuse + specular) * nDotL * (1.0 - coatFresnel) + coatSpecular * coatNDotL) * radiance;
    }
    return (diffuse + specular) * nDotL * radiance;
}

// Normal along which the shadow test offsets the receiver: the lit side's. Light reaches translucent
// surfaces from behind too, so their test offsets towards the light rather than towards the viewer.
float3 receiverNormal(float3 geometricNormal, float3 toLight, bool translucent)
{
    return translucent && dot(geometricNormal, toLight) < 0.0 ? -geometricNormal : geometricNormal;
}

// Specular light of the environment for the reflectance @p f0 along @p normal (the split sum, single scattering
// only), before occlusion.
float3 environmentSpecular(Surface surface, float3 normal, float roughness, float3 f0)
{
    const float nDotV = max(dot(normal, surface.view), 1e-4);
    const float2 brdf = g_brdfLut.SampleLevel(g_clampSampler, float2(nDotV, roughness), 0.0).xy;
    const float3 reflected = reflect(-surface.view, normal);
    const float3x3 toEnvironment = (float3x3)g_view.skyRotation;
    const float3 radiance =
        g_specularCube.SampleLevel(g_clampSampler, mul(toEnvironment, reflected), roughness * g_view.sky.y).rgb *
        g_view.sky.x;
    return (f0 * brdf.x + brdf.y) * radiance;
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
    float3 irradiance = shIrradiance(g_view.sh, mul(toEnvironment, surface.normal)) / kPi;
    if (kShadingModel == kShadingModelTwoSidedFoliage && surface.transmission.w > 0.0)
    {
        // The sky and ground light reaching the back face pass through translucent surfaces.
        const float3 behind = shIrradiance(g_view.sh, mul(toEnvironment, -surface.normal)) / kPi;
        irradiance = irradiance * (1.0 - surface.transmission.w) + behind * surface.transmission.rgb;
    }
    const float3 reflected = reflect(-surface.view, surface.normal);
    const float3 radiance =
        g_specularCube.SampleLevel(g_clampSampler, mul(toEnvironment, reflected), roughness * g_view.sky.y).rgb *
        g_view.sky.x;
    const float specularOcclusion =
        saturate(pow(nDotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion);
    const float3 base = singleScatter * radiance * specularOcclusion +
                        (multiScatter * multiScatterEnergy + diffuseWeight) * irradiance * occlusion;
    if (kShadingModel == kShadingModelClearCoat)
    {
        // The coat reflects the sky at its own roughness and passes the rest of the light to the base.
        const float3 n = surface.coat.xyz;
        const float coatNDotV = max(dot(n, surface.view), 1e-4);
        const float coatFresnel = fresnelSchlick(kClearCoatF0.xxx, coatNDotV).x * surface.coat.w;
        const float coatOcclusion = saturate(pow(coatNDotV + occlusion, exp2(-16.0 * surface.coatRoughness - 1.0)) -
                                             1.0 + occlusion);
        const float3 coat = environmentSpecular(surface, n, surface.coatRoughness, kClearCoatF0.xxx);
        return base * (1.0 - coatFresnel) + coat * surface.coat.w * coatOcclusion;
    }
    return base;
}
