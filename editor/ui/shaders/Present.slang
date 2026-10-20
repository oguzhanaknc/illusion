// Copies the UI target to the output. The UI target holds sRGB-encoded values in an UNORM texture: for an
// _SRGB output the values are decoded here, so that the hardware's encoding writes them back unchanged;
// UNORM outputs receive them as they are.

// 16 bytes, like PresentPush in UiRenderer.cpp. Scalars only: a uint3 would start at offset 16.
struct PresentPush
{
    uint decodeSrgb;
    uint padding0;
    uint padding1;
    uint padding2;
};
[[vk::push_constant]] ConstantBuffer<PresentPush> g_push;

Texture2D g_source : register(t0);

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 clip = float2(float((vertexId << 1) & 2), float(vertexId & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    return output;
}

float3 srgbToLinear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = g_source.Load(int3(int2(input.position.xy), 0));
    if (g_push.decodeSrgb != 0)
    {
        color.rgb = srgbToLinear(color.rgb);
    }
    return color;
}
