// Depth-aware 5x5 blur of the raw GTAO term: removes the per-pixel noise without bleeding across
// depth discontinuities, then clamps to [0, 1] and applies the intensity exponent.

#include "PostCommon.slang"

Texture2D<float> g_ambientOcclusion : register(t0);
Texture2D<float> g_depth : register(t1);
[[vk::image_format("r32f")]] RWTexture2D<float> g_output : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void gtaoBlurMain(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(g_post.size.xy)))
    {
        return;
    }
    const int2 pixel = int2(id.xy);
    const int2 last = int2(g_post.size.xy) - 1;
    const float centreDepth = g_depth.Load(int3(pixel, 0));
    if (centreDepth <= 0.0)
    {
        g_output[id.xy] = 1.0;
        return;
    }
    // Reversed infinite projection: view distance is proportional to 1 / depth.
    const float centreDistance = 1.0 / centreDepth;
    float sum = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const int2 neighbour = clamp(pixel + int2(x, y), int2(0, 0), last);
            const float depth = g_depth.Load(int3(neighbour, 0));
            const float distance = depth > 0.0 ? 1.0 / depth : 1e30;
            const float weight = saturate(1.0 - abs(distance - centreDistance) / (0.05 * centreDistance));
            sum += g_ambientOcclusion.Load(int3(neighbour, 0)) * weight;
            weightSum += weight;
        }
    }
    const float visibility = weightSum > 0.0 ? saturate(sum / weightSum) : 1.0;
    g_output[id.xy] = pow(visibility, g_post.ao.y);
}
