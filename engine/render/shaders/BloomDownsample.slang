// Bloom downsample (Jimenez 2014, 13 bilinear taps). The first pass from the full-resolution image
// weights each group by 1 / (1 + luma) (Karis average) so single bright pixels do not flicker.

#include "PostCommon.slang"

Texture2D g_source : register(t0);
SamplerState g_linearClamp : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> g_output : register(u0);

float3 tap(float2 uv, float2 texel, float2 offset)
{
    return g_source.SampleLevel(g_linearClamp, uv + offset * texel, 0.0).rgb;
}

float karisWeight(float3 c)
{
    return 1.0 / (1.0 + luminance(c));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void downsampleMain(uint3 id: SV_DispatchThreadID)
{
    uint width;
    uint height;
    g_output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    uint sourceWidth;
    uint sourceHeight;
    g_source.GetDimensions(sourceWidth, sourceHeight);
    const float2 texel = 1.0 / float2(sourceWidth, sourceHeight);
    const float2 uv = (float2(id.xy) + 0.5) / float2(width, height);

    const float3 a = tap(uv, texel, float2(-2.0, -2.0));
    const float3 b = tap(uv, texel, float2(0.0, -2.0));
    const float3 c = tap(uv, texel, float2(2.0, -2.0));
    const float3 d = tap(uv, texel, float2(-2.0, 0.0));
    const float3 e = tap(uv, texel, float2(0.0, 0.0));
    const float3 f = tap(uv, texel, float2(2.0, 0.0));
    const float3 g = tap(uv, texel, float2(-2.0, 2.0));
    const float3 h = tap(uv, texel, float2(0.0, 2.0));
    const float3 i = tap(uv, texel, float2(2.0, 2.0));
    const float3 j = tap(uv, texel, float2(-1.0, -1.0));
    const float3 k = tap(uv, texel, float2(1.0, -1.0));
    const float3 l = tap(uv, texel, float2(-1.0, 1.0));
    const float3 m = tap(uv, texel, float2(1.0, 1.0));

    // Five overlapping 2x2 boxes: the centre one weighted 0.5, the corner ones 0.125 each.
    const float3 boxes[5] = {(j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
                             (d + e + g + h) * 0.25, (e + f + h + i) * 0.25};
    const float weights[5] = {0.5, 0.125, 0.125, 0.125, 0.125};
    float3 sum = 0.0;
    float weightSum = 0.0;
    for (uint box = 0; box < 5; ++box)
    {
        const float w = weights[box] * (g_push.firstPass != 0 ? karisWeight(boxes[box]) : 1.0);
        sum += boxes[box] * w;
        weightSum += w;
    }
    g_output[id.xy] = float4(sum / weightSum, 1.0);
}
