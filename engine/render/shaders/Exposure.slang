// Automatic exposure (PostEffects::autoExposure): meters the HDR image with a centre-weighted average of
// log luminance over a 64x64 grid, and moves the exposure stored in a 1x1 texture towards the one that puts
// that average at middle grey, within the limits. One group of 8x8 threads.

#include "PostCommon.slang"

Texture2D g_source : register(t0);
SamplerState g_linearClamp : register(s0);
[[vk::image_format("r32f")]] RWTexture2D<float> g_exposure : register(u0);

groupshared float s_sum[64];
groupshared float s_weight[64];

[shader("compute")]
[numthreads(8, 8, 1)]
void exposureMain(uint3 thread: SV_GroupThreadID)
{
    float sum = 0.0;
    float weight = 0.0;
    for (uint y = 0; y < 8; ++y)
    {
        for (uint x = 0; x < 8; ++x)
        {
            const float2 uv = (float2(thread.xy * 8 + uint2(x, y)) + 0.5) / 64.0;
            const float3 color = g_source.SampleLevel(g_linearClamp, uv, 0.0).rgb;
            const float2 centre = uv - 0.5;
            const float w = exp(-dot(centre, centre) * 6.0); // the centre of the view counts most
            sum += log2(max(luminance(color), 1e-5)) * w;
            weight += w;
        }
    }
    const uint index = thread.y * 8 + thread.x;
    s_sum[index] = sum;
    s_weight[index] = weight;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (index < stride)
        {
            s_sum[index] += s_sum[index + stride];
            s_weight[index] += s_weight[index + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0)
    {
        const float average = exp2(s_sum[0] / max(s_weight[0], 1e-6));
        const float target = clamp(0.18 / max(average, 1e-6), g_post.exposure.z, g_post.exposure.w);
        const float previous = g_exposure[uint2(0, 0)];
        const bool valid = previous > 0.0 && !isinf(previous) && !isnan(previous);
        // Adapt in stops, like an eye.
        g_exposure[uint2(0, 0)] = valid ? exp2(lerp(log2(previous), log2(target), saturate(g_post.adapt.x))) : target;
    }
}
