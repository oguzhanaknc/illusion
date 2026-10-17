// Temporal anti-aliasing resolve: reprojects the history with the depth buffer and the previous camera,
// clips it to the current 3x3 neighbourhood (variance clipping in YCoCg) and blends in the current
// frame. Colours are blended after a reversible tonemap so bright pixels do not dominate.

#include "PostCommon.slang"

Texture2D g_current : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D g_history : register(t2);
SamplerState g_linearClamp : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> g_output : register(u0);

float3 compress(float3 c)
{
    return c / (1.0 + luminance(c));
}

float3 decompress(float3 c)
{
    return c / max(1.0 - luminance(c), 1e-4);
}

float3 toYCoCg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 fromYCoCg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void taaMain(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(g_post.size.xy)))
    {
        return;
    }
    const int2 pixel = int2(id.xy);
    const int2 last = int2(g_post.size.xy) - 1;

    // Neighbourhood statistics; the closest depth reprojects edges with the foreground's motion.
    float3 mean = 0.0;
    float3 meanSquared = 0.0;
    float3 current = 0.0;
    float closestDepth = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 neighbour = clamp(pixel + int2(x, y), int2(0, 0), last);
            const float3 colour = toYCoCg(compress(g_current.Load(int3(neighbour, 0)).rgb));
            mean += colour;
            meanSquared += colour * colour;
            if (x == 0 && y == 0)
            {
                current = colour;
            }
            closestDepth = max(closestDepth, g_depth.Load(int3(neighbour, 0))); // reversed-Z: larger is closer
        }
    }
    mean /= 9.0;
    const float3 deviation = sqrt(max(meanSquared / 9.0 - mean * mean, 0.0));

    const float2 uv = (float2(pixel) + 0.5) * g_post.size.zw;
    // Homogeneous reprojection also works for the sky (depth 0, a point at infinity).
    const float4 world = mul(g_post.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, closestDepth, 1.0));
    const float4 previousClip = mul(g_post.previousViewProjection, world);
    const float2 previousNdc = previousClip.xy / previousClip.w;
    const float2 previousUv = float2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);

    float3 result = current;
    if (g_post.temporal.y > 0.5 && previousClip.w > 0.0 && all(previousUv >= 0.0) && all(previousUv <= 1.0))
    {
        float3 history = toYCoCg(compress(g_history.SampleLevel(g_linearClamp, previousUv, 0.0).rgb));
        // Clip the history towards the neighbourhood mean (Salvi 2016 variance clipping).
        const float3 extent = deviation * 1.25 + 1e-4;
        const float3 offset = history - mean;
        const float3 units = abs(offset / extent);
        const float maxUnit = max(units.x, max(units.y, units.z));
        if (maxUnit > 1.0)
        {
            history = mean + offset / maxUnit;
        }
        result = lerp(history, current, g_post.temporal.x);
    }
    g_output[id.xy] = float4(decompress(fromYCoCg(result)), 1.0);
}
