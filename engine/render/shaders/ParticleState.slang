// The state of one GPU-simulated particle, shared by ParticleSimulate.slang and ParticlesGpu.slang. Layout
// must match ParticleRenderer.cpp.

struct ParticleState
{
    float4 positionAge;  // xyz: position (world, or the emitter's space for local emitters), w: age in s
    float4 velocityLife; // xyz: velocity in m/s, w: lifetime in s
};
