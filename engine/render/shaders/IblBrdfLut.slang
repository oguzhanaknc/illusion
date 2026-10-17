// Split-sum BRDF lookup table (Karis 2013).

#include "IblCommon.slang"

[[vk::image_format("rgba16f")]] RWTexture2D<float4> g_outputLut : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void integrateBrdf(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= g_params.size))
    {
        return;
    }
    const float nDotV = (float(id.x) + 0.5) / float(g_params.size);
    const float roughness = (float(id.y) + 0.5) / float(g_params.size);
    const float alpha = roughness * roughness;
    const float3 v = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    const float3 n = float3(0.0, 0.0, 1.0);
    float a = 0.0;
    float b = 0.0;
    for (uint i = 0; i < g_params.sampleCount; ++i)
    {
        const float3 h = importanceSampleGgx(hammersley(i, g_params.sampleCount), n, alpha);
        const float vDotH = saturate(dot(v, h));
        const float3 l = 2.0 * vDotH * h - v;
        const float nDotL = saturate(l.z);
        const float nDotH = saturate(h.z);
        if (nDotL > 0.0)
        {
            // Visibility * 4 * nDotL * vDotH / nDotH: the GGX-sampled estimator of the specular integral.
            const float g = visibilitySmithGgxCorrelated(nDotV, nDotL, alpha) * 4.0 * nDotL * vDotH / max(nDotH, 1e-6);
            const float fresnel = pow(1.0 - vDotH, 5.0);
            a += (1.0 - fresnel) * g;
            b += fresnel * g;
        }
    }
    g_outputLut[id.xy] = float4(a / float(g_params.sampleCount), b / float(g_params.sampleCount), 0.0, 1.0);
}
