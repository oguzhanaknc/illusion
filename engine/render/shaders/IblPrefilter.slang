// One mip of the GGX-prefiltered specular cube map.

#include "IblCommon.slang"

TextureCube g_sourceCube : register(t0);
SamplerState g_sampler : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> g_outputCube : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void prefilterSpecular(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= g_params.size))
    {
        return;
    }
    const float3 n = cubeDirection(id.xy, id.z, g_params.size);
    if (g_params.roughness <= 0.0)
    {
        g_outputCube[id] = float4(g_sourceCube.SampleLevel(g_sampler, n, 0.0).rgb, 1.0);
        return;
    }
    const float alpha = g_params.roughness * g_params.roughness;
    const float texelSolidAngle = 4.0 * kPi / (6.0 * g_params.sourceSize * g_params.sourceSize);
    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < g_params.sampleCount; ++i)
    {
        const float3 h = importanceSampleGgx(hammersley(i, g_params.sampleCount), n, alpha);
        const float nDotH = saturate(dot(n, h));
        const float3 l = 2.0 * nDotH * h - n;
        const float nDotL = dot(n, l);
        if (nDotL > 0.0)
        {
            // pdf of l is D * nDotH / (4 vDotH) and vDotH == nDotH here.
            const float pdf = distributionGgx(nDotH, alpha) * 0.25;
            const float sampleSolidAngle = 1.0 / (float(g_params.sampleCount) * pdf + 1e-6);
            const float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
            sum += g_sourceCube.SampleLevel(g_sampler, l, lod).rgb * nDotL;
            weight += nDotL;
        }
    }
    g_outputCube[id] = float4(sum / max(weight, 1e-6), 1.0);
}

// Split-sum BRDF table: x = scale, y = bias of F0, indexed by (n.v, perceptual roughness).
