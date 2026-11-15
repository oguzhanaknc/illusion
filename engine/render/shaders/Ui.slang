// The game UI (ADR 0057): canvas-space quads, y down, with colour images or signed distance field text.
// Matches `Push` in UiRenderer.cpp (24 bytes).
struct Push
{
    float2 scale;      // 2 / canvas size in units
    float sdfSpread;   // atlas texels on each side of an edge; 0 for colour images
    uint flags;        // bit 0: the target is UNORM, encode sRGB in the shader
    float sdfOutline;  // outline width in atlas texels; 0 for none
    uint outlineColor; // sRGB RGBA8 with straight alpha, red in the lowest byte
};
[[vk::push_constant]] ConstantBuffer<Push> g_push;
Texture2D<float4> g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

float3 srgbToLinear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position.x * g_push.scale.x - 1.0, 1.0 - input.position.y * g_push.scale.y,
                             0.0, 1.0);
    output.uv = input.uv;
    output.color = float4(srgbToLinear(input.color.rgb), input.color.a);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float4 texel = g_texture.Sample(g_sampler, input.uv);
    // Atlas texels per screen pixel; the push constant keeps this branch uniform.
    const float2 texelsPerPixel = fwidth(input.uv);
    float4 color = input.color;
    if (g_push.sdfSpread > 0.0)
    {
        uint width;
        uint height;
        g_texture.GetDimensions(width, height);
        // The field goes from 0 to 1 across 2 * spread texels; screenRange is that distance in pixels.
        const float2 unitRange = (2.0 * g_push.sdfSpread) / float2(width, height);
        const float2 screenTexSize = 1.0 / max(texelsPerPixel, float2(1e-8));
        const float screenRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        color.a *= saturate((texel.r - 0.5) * screenRange + 0.5);
        if (g_push.sdfOutline > 0.0)
        {
            // The outline's edge lies sdfOutline texels outside the glyph's: the field is 0.5 at the edge and
            // falls by 1 / (2 * spread) per texel. The fill is composited over it, premultiplied.
            const uint packed = g_push.outlineColor;
            const float4 outline = float4(srgbToLinear(float3(float(packed & 0xFFu), float((packed >> 8u) & 0xFFu),
                                                              float((packed >> 16u) & 0xFFu)) /
                                                       255.0),
                                          float(packed >> 24u) / 255.0);
            const float threshold = 0.5 - g_push.sdfOutline / (2.0 * g_push.sdfSpread);
            const float outlineAlpha = outline.a * saturate((texel.r - threshold) * screenRange + 0.5);
            const float alpha = color.a + outlineAlpha * (1.0 - color.a);
            const float3 premultiplied = color.rgb * color.a + outline.rgb * outlineAlpha * (1.0 - color.a);
            color = float4(alpha > 0.0 ? premultiplied / alpha : float3(0.0), alpha);
        }
    }
    else
    {
        color *= texel;
    }
    float3 rgb = color.rgb;
    if ((g_push.flags & 1u) != 0u)
    {
        rgb = linearToSrgb(saturate(rgb));
    }
    return float4(rgb * color.a, color.a);
}
