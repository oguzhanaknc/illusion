// Environment background: a full-screen triangle at the far plane (depth 0 with reversed-Z), drawn
// after opaque geometry so the depth test rejects covered pixels.

#include "Common.slang"

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
TextureCube g_skybox : register(t3, space0);
SamplerState g_clampSampler : register(s0, space0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 clip : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 clip = float2(float((vertexId << 1) & 2), float(vertexId & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    output.clip = clip;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Unproject a point on the near plane (depth 1) and look from the camera through it.
    const float4 nearPoint = mul(g_view.inverseViewProjection, float4(input.clip, 1.0, 1.0));
    const float3 direction = (g_view.effects.z > 0.5 ? g_view.cameraForward.xyz : normalize(nearPoint.xyz / nearPoint.w - g_view.cameraPosition.xyz));
    const float3 environmentDirection = mul((float3x3)g_view.skyRotation, direction);
    const float3 radiance = g_skybox.SampleLevel(g_clampSampler, environmentDirection, 0.0).rgb * g_view.sky.z;
    return float4(radiance, 1.0);
}
