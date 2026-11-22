// Lighting of the forward pass shared by ForwardPbr.slang and the material shaders' passes: Cook-Torrance
// GGX specular, Lambert diffuse with diffuse transmission, and image-based lighting. Needs the bindings of
// ForwardBindings.slang.

#pragma once

#include "ForwardBindings.slang"

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
    float4 transmission; // rgb: transmitted tint, w: fraction of diffuse light transmitted (0 for most)
};

// Direct lighting from one light. Translucent surfaces (KHR_materials_diffuse_transmission) split their
// diffuse light between this side and the other: light that reaches the back face leaves this side as
// Lambertian light tinted by the transmission colour.
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
    const float3 reflected = (diffuse * (1.0 - surface.transmission.w) + specular) * nDotL;
    const float backDotL = saturate(-dot(surface.normal, l));
    const float3 transmitted = surface.transmission.rgb * (1.0 - surface.f0) * surface.albedo / kPi * backDotL;
    return (reflected + transmitted) * radiance;
}

// Normal along which the shadow test offsets the receiver: the lit side's. Light reaches translucent
// surfaces from behind too, so their test offsets towards the light rather than towards the viewer.
float3 receiverNormal(float3 geometricNormal, float3 toLight, bool translucent)
{
    return translucent && dot(geometricNormal, toLight) < 0.0 ? -geometricNormal : geometricNormal;
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
    if (surface.transmission.w > 0.0)
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
    return singleScatter * radiance * specularOcclusion +
           (multiScatter * multiScatterEnergy + diffuseWeight) * irradiance * occlusion;
}
