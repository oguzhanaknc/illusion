// Shadow casters: depth-only rendering into one cascade of the shadow map. The fragment entry point is
// used only for alpha-masked materials.

#include "Common.slang"

struct ShadowPush
{
    uint instanceOffset;
    uint cascade;
};

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
StructuredBuffer<InstanceData> g_instances : register(t0, space0);
[[vk::push_constant]] ConstantBuffer<ShadowPush> g_push;

struct MaterialConstants
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float alphaCutoff;
};

ConstantBuffer<MaterialConstants> g_material : register(b0, space1);
Texture2D g_baseColorTexture : register(t0, space1);
SamplerState g_sampler : register(s0, space1);

struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instanceId: SV_InstanceID)
{
    const InstanceData instance = g_instances[g_push.instanceOffset + instanceId];
    VertexOutput output;
    output.position = mul(g_view.cascades[g_push.cascade].viewProjection, mul(instance.world, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
    const float alpha = g_baseColorTexture.Sample(g_sampler, input.uv).a * g_material.baseColorFactor.a;
    if (alpha < g_material.alphaCutoff)
    {
        discard;
    }
}
