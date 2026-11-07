// Final composite: chromatic aberration, bloom mixed into the HDR image, exposure (manual or automatic),
// white balance, contrast and saturation, ACES or AgX tonemapping, vignette, (for UNORM targets) sRGB
// encoding and film grain. Drawn as a full-screen triangle into the output target.

#include "PostCommon.slang"

Texture2D g_color : register(t0);
Texture2D g_bloom : register(t1);
Texture2D<float> g_exposure : register(t2); // automatic exposure, 1x1
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

// AgX (Troy Sobotka) with the polynomial fit of its default contrast curve (Benjamin Wrensch).
float3 agxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 tonemapAgx(float3 color)
{
    const float3x3 inset = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                    0.0784335999999992, 0.878468636469772, 0.0784336,
                                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                     -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                     -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    float3 x = mul(max(color, 1e-10), inset);
    x = (clamp(log2(x), minEv, maxEv) - minEv) / (maxEv - minEv);
    x = mul(agxContrast(x), outset);
    // The curve's output is display-encoded (gamma 2.2); return linear light like the ACES path.
    return pow(saturate(x), 2.2);
}

float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const int2 pixel = int2(input.position.xy);
    float3 color = g_color.Load(int3(pixel, 0)).rgb;
    const float aberration = g_post.balance.w;
    if (aberration > 0.0)
    {
        // Red and blue spread apart towards the corners, as through a simple lens.
        const float2 offset = (input.uv - 0.5) * 2.0 * aberration * g_post.size.zw;
        color.r = g_color.SampleLevel(g_linearClamp, input.uv + offset, 0.0).r;
        color.b = g_color.SampleLevel(g_linearClamp, input.uv - offset, 0.0).b;
    }
    if (g_post.bloom.z > 0.5)
    {
        const float3 bloom = g_bloom.SampleLevel(g_linearClamp, input.uv, 0.0).rgb / g_post.bloom.x;
        color = lerp(color, bloom, g_post.bloom.y);
    }
    if (g_post.output.z > 0.5)
    {
        // Diagnostic views show their values as they are.
        float3 raw = saturate(color);
        return float4(g_post.output.y > 0.5 ? linearToSrgb(raw) : raw, 1.0);
    }
    float exposure = g_post.output.x * g_post.exposure.y;
    if (g_post.exposure.x > 0.5)
    {
        exposure *= g_exposure.Load(int3(0, 0, 0));
    }
    color *= exposure * g_post.balance.rgb;
    const float contrast = g_post.grading.x;
    if (contrast != 1.0)
    {
        color = 0.18 * pow(max(color, 0.0) / 0.18, contrast);
    }
    const float grey = luminance(color);
    color = max(lerp(float3(grey, grey, grey), color, g_post.grading.y), 0.0);
    float3 mapped = g_post.output.w > 0.5 ? tonemapAgx(color) : tonemapAces(color);
    const float vignette = g_post.grading.z;
    if (vignette > 0.0)
    {
        const float2 centre = (input.uv - 0.5) * float2(g_post.size.x * g_post.size.w, 1.0);
        mapped *= 1.0 - vignette * smoothstep(0.25, 0.95, length(centre) * 1.3);
    }
    if (g_post.output.y > 0.5)
    {
        mapped = linearToSrgb(mapped);
    }
    const float grain = g_post.grading.w;
    if (grain > 0.0)
    {
        // Animated, strongest in the mid-tones.
        const float noise = interleavedGradientNoise(float2(pixel) + g_post.adapt.y * float2(17.0, 31.0)) - 0.5;
        mapped += noise * grain * 0.12 * (1.0 - abs(mapped * 2.0 - 1.0));
    }
    return float4(saturate(mapped), 1.0);
}
