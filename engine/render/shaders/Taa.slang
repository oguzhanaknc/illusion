// Temporal anti-aliasing resolve: reprojects the history with per-pixel motion vectors (or, for the sky,
// the previous camera), clips it to the current 3x3 neighbourhood (variance clipping in YCoCg) and blends
// in the current frame. Colours are blended after a reversible tonemap so bright pixels do not dominate.

#include "PostCommon.slang"

Texture2D g_current : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D g_history : register(t2);
Texture2D<float2> g_motion : register(t3);
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

// Catmull-Rom history sample from 9 bilinear taps (Jimenez 2016): sharper than bilinear, so repeated
// reprojection of moving content does not blur it.
float3 sampleHistory(float2 uv)
{
    const float2 size = g_post.size.xy;
    const float2 position = uv * size;
    const float2 centre = floor(position - 0.5) + 0.5;
    const float2 f = position - centre;
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);
    const float2 w12 = w1 + w2;
    const float2 offset12 = w2 / w12;
    const float2 uv0 = (centre - 1.0) * g_post.size.zw;
    const float2 uv3 = (centre + 2.0) * g_post.size.zw;
    const float2 uv12 = (centre + offset12) * g_post.size.zw;
    float3 result = 0.0;
    result += g_history.SampleLevel(g_linearClamp, float2(uv0.x, uv0.y), 0.0).rgb * w0.x * w0.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv12.x, uv0.y), 0.0).rgb * w12.x * w0.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv3.x, uv0.y), 0.0).rgb * w3.x * w0.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv0.x, uv12.y), 0.0).rgb * w0.x * w12.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv12.x, uv12.y), 0.0).rgb * w12.x * w12.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv3.x, uv12.y), 0.0).rgb * w3.x * w12.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv0.x, uv3.y), 0.0).rgb * w0.x * w3.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv12.x, uv3.y), 0.0).rgb * w12.x * w3.y;
    result += g_history.SampleLevel(g_linearClamp, float2(uv3.x, uv3.y), 0.0).rgb * w3.x * w3.y;
    return max(result, 0.0); // the negative lobes can undershoot
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
    int2 closestPixel = pixel;
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
            const float depth = g_depth.Load(int3(neighbour, 0));
            if (depth > closestDepth) // reversed-Z: larger is closer
            {
                closestDepth = depth;
                closestPixel = neighbour;
            }
        }
    }
    mean /= 9.0;
    const float3 deviation = sqrt(max(meanSquared / 9.0 - mean * mean, 0.0));

    const float2 uv = (float2(pixel) + 0.5) * g_post.size.zw;
    float2 previousUv;
    bool reprojected = true;
    if (closestDepth > 0.0)
    {
        // Geometry: the prepass wrote its motion (camera and object) for the closest neighbour.
        previousUv = uv - g_motion.Load(int3(closestPixel, 0));
    }
    else
    {
        // Sky: homogeneous reprojection of the point at infinity with the previous camera.
        const float4 world = mul(g_post.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0));
        const float4 previousClip = mul(g_post.previousViewProjection, world);
        reprojected = previousClip.w > 0.0;
        const float2 previousNdc = previousClip.xy / previousClip.w;
        previousUv = float2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);
    }

    float3 result = current;
    if (g_post.temporal.y > 0.5 && reprojected && all(previousUv >= 0.0) && all(previousUv <= 1.0))
    {
        float3 history = toYCoCg(compress(sampleHistory(previousUv)));
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
