// Bloom upsample: adds a 3x3 tent-filtered copy of the next smaller level to this level. After the
// whole chain, level 0 holds the sum of all levels; Tonemap.slang divides by the level count so a
// uniform image keeps its brightness.

#include "PostCommon.slang"

Texture2D g_smaller : register(t0);
SamplerState g_linearClamp : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> g_output : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void upsampleMain(uint3 id: SV_DispatchThreadID)
{
    uint width;
    uint height;
    g_output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    const float2 texel = 1.0 / float2(width, height);
    const float2 uv = (float2(id.xy) + 0.5) * texel;
    float3 sum = g_smaller.SampleLevel(g_linearClamp, uv, 0.0).rgb * 4.0;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(-texel.x, 0.0), 0.0).rgb * 2.0;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(texel.x, 0.0), 0.0).rgb * 2.0;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(0.0, -texel.y), 0.0).rgb * 2.0;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(0.0, texel.y), 0.0).rgb * 2.0;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(-texel.x, -texel.y), 0.0).rgb;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(texel.x, -texel.y), 0.0).rgb;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(-texel.x, texel.y), 0.0).rgb;
    sum += g_smaller.SampleLevel(g_linearClamp, uv + float2(texel.x, texel.y), 0.0).rgb;
    g_output[id.xy] = float4(g_output[id.xy].rgb + sum / 16.0, 1.0);
}
