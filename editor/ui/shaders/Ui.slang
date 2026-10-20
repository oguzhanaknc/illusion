// Dear ImGui draw lists. Vertices are in window coordinates; colours and textures are used as they are
// (gamma space), so blending happens on sRGB-encoded values like in every other UI toolkit. The UI target is
// RGBA8_UNORM; Present.slang decodes it for sRGB swapchains.

struct UiPush
{
    float2 scale;     // window coordinates to clip space
    float2 translate;
};
[[vk::push_constant]] ConstantBuffer<UiPush> g_push;

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * g_push.scale + g_push.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color * g_texture.Sample(g_sampler, input.uv);
}
