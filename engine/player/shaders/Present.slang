// Copies the player's 3D view (an RGBA8_UNORM target that holds sRGB-encoded colour) onto the back buffer,
// scaled with bilinear filtering when the render scale is not 1. Drawn as a full-screen triangle.

Texture2D g_source : register(t0);
SamplerState g_linearClamp : register(s0);

struct PresentPush
{
    uint decodeSrgb; // 1 when the back buffer is an sRGB format, which encodes again on write
    uint padding0;
    uint padding1;
    uint padding2;
};
[[vk::push_constant]] ConstantBuffer<PresentPush> g_push;

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

float3 srgbToLinear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = g_source.SampleLevel(g_linearClamp, input.uv, 0.0);
    if (g_push.decodeSrgb != 0)
    {
        color.rgb = srgbToLinear(color.rgb);
    }
    return float4(color.rgb, 1.0);
}
