// Final composite: bloom mixed into the HDR image, exposure, ACES tonemapping and (for UNORM targets)
// sRGB encoding. Drawn as a full-screen triangle into the output target.

#include "PostCommon.slang"

Texture2D g_color : register(t0);
Texture2D g_bloom : register(t1);
SamplerState g_linearClamp : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 clip = float2(float((vertexId << 1) & 2), float(vertexId & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    output.uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    return output;
}

float3 tonemapAces(float3 x)
{
    // Narkowicz 2015 fit of the ACES filmic curve.
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 color = g_color.Load(int3(int2(input.position.xy), 0)).rgb;
    if (g_post.bloom.z > 0.5)
    {
        const float3 bloom = g_bloom.SampleLevel(g_linearClamp, input.uv, 0.0).rgb / g_post.bloom.x;
        color = lerp(color, bloom, g_post.bloom.y);
    }
    float3 mapped = g_post.output.z > 0.5 ? saturate(color) : tonemapAces(color * g_post.output.x);
    if (g_post.output.y > 0.5)
    {
        mapped = linearToSrgb(mapped);
    }
    return float4(mapped, 1.0);
}
