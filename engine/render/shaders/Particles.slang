// CPU-simulated particles (ParticleSimulation::Cpu): ParticleRenderer uploads each live particle's position,
// width and colour, and this draws them (see ParticlesCommon.slang).

#include "ParticlesCommon.slang"

struct GpuParticle
{
    float4 positionSize; // xyz: world position, w: width
    float4 color;        // linear colour and alpha
};

StructuredBuffer<GpuParticle> g_instances : register(t0, space0);

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID, uint instanceId: SV_InstanceID)
{
    const GpuParticle particle = g_instances[g_push.firstInstance + instanceId];
    return billboard(vertexId, particle.positionSize.xyz, particle.positionSize.w, particle.color);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return shadeParticle(input);
}
