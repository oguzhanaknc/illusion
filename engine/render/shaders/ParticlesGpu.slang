// GPU-simulated particles (ParticleSimulation::Gpu): ParticleSimulate.slang keeps their state in a buffer and
// writes the draw arguments, so the CPU never sees them; colour and width follow each particle's life (see
// ParticlesCommon.slang).

#include "ParticlesCommon.slang"
#include "ParticleState.slang"

StructuredBuffer<ParticleState> g_states : register(t0, space0);

float3 rotate(float4 q, float3 v)
{
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID, uint instanceId: SV_InstanceID)
{
    const ParticleState state = g_states[instanceId];
    const float life = saturate(state.positionAge.w / max(state.velocityLife.w, 1e-6));
    float3 world = state.positionAge.xyz;
    if (g_push.modelPosition.w > 0.5)
    {
        world = g_push.modelPosition.xyz + rotate(g_push.modelRotation, world * g_push.modelScale.xyz);
    }
    const float4 color = lerp(g_push.colorStart, g_push.colorEnd, life);
    const float width = lerp(g_push.size.x, g_push.size.y, life);
    return billboard(vertexId, world, width, color);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return shadeParticle(input);
}
