// GPU particle simulation (ParticleSimulation::Gpu, ADR 0047). Each emitter has two state buffers and a pair
// of live counts; a step reads the current buffer and appends to the other:
//   simulateMain - ages, moves and keeps the live particles (dead ones are dropped by not being appended);
//   spawnMain    - appends this step's births, made from the emitter's seed and the step number;
//   finishMain   - clamps the new count to the capacity, empties the old one and writes the draw arguments.
// resetMain empties both on a new emitter, play() or clear(). Layouts must match ParticleRenderer.cpp.

#include "ParticleState.slang"

struct SimulateConstants
{
    float4 acceleration; // xyz: gravity * gravityFactor in the simulation space, w: drag
    float4 timing;       // x: seconds of the step, y: lifetime min, z: lifetime max
    float4 speed;        // x: min, y: max, z: spread (radians), w: shape (0 point, 1 sphere, 2 box)
    float4 shape;        // xyz: box half extents, w: sphere radius
    float4 position;     // the emitter's position; w: 1 when births are placed in world space
    float4 rotation;     // the emitter's rotation (quaternion xyzw)
    float4 scale;        // the emitter's scale
    uint4 counts;        // x: births, y: capacity, z: seed, w: step
    uint4 state;         // x: the current buffer (0 or 1)
};

ConstantBuffer<SimulateConstants> g_sim : register(b0);
StructuredBuffer<ParticleState> g_input : register(t0);
RWStructuredBuffer<ParticleState> g_output : register(u0);
RWByteAddressBuffer g_counters : register(u1);  // uint[2]: live particles of buffers 0 and 1
RWByteAddressBuffer g_arguments : register(u2); // vertexCount, instanceCount, startVertex, startInstance

static const float kPi = 3.14159265358979;

uint currentCount() { return g_counters.Load(g_sim.state.x * 4); }
uint nextOffset() { return (1u - g_sim.state.x) * 4u; }

uint hash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(inout uint seed)
{
    seed = hash(seed);
    return float(seed >> 8) * (1.0 / 16777216.0);
}

float3 rotate(float4 q, float3 v)
{
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// A direction within @p spread radians of +Y, uniform over that cap of the sphere.
float3 directionWithin(float spread, inout uint seed)
{
    const float cosMax = cos(clamp(spread, 0.0, kPi));
    const float cosTheta = 1.0 - random(seed) * (1.0 - cosMax);
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * kPi * random(seed);
    return float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

[shader("compute")]
[numthreads(1, 1, 1)]
void resetMain()
{
    g_counters.Store2(0, uint2(0, 0));
    g_arguments.Store4(0, uint4(6, 0, 0, 0));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void simulateMain(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= currentCount())
    {
        return;
    }
    ParticleState particle = g_input[id.x];
    const float seconds = g_sim.timing.x;
    particle.positionAge.w += seconds;
    if (particle.positionAge.w >= particle.velocityLife.w)
    {
        return;
    }
    const float keep = max(0.0, 1.0 - max(g_sim.acceleration.w, 0.0) * seconds);
    const float3 velocity = (particle.velocityLife.xyz + g_sim.acceleration.xyz * seconds) * keep;
    particle.velocityLife.xyz = velocity;
    particle.positionAge.xyz += velocity * seconds;
    uint slot;
    g_counters.InterlockedAdd(nextOffset(), 1, slot);
    g_output[slot] = particle;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void spawnMain(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= g_sim.counts.x)
    {
        return;
    }
    uint slot;
    g_counters.InterlockedAdd(nextOffset(), 1, slot);
    if (slot >= g_sim.counts.y)
    {
        return; // full: finishMain clamps the count
    }
    uint seed = hash(g_sim.counts.z ^ hash(g_sim.counts.w ^ hash(id.x)));
    float3 offset = float3(0.0, 0.0, 0.0);
    if (g_sim.speed.w > 1.5)
    {
        offset = (float3(random(seed), random(seed), random(seed)) * 2.0 - 1.0) * max(g_sim.shape.xyz, 0.0);
    }
    else if (g_sim.speed.w > 0.5)
    {
        offset = directionWithin(kPi, seed) * (max(g_sim.shape.w, 0.0) * pow(random(seed), 1.0 / 3.0));
    }
    const float3 direction = directionWithin(g_sim.speed.z, seed);
    const float speed = lerp(g_sim.speed.x, g_sim.speed.y, random(seed));
    const float lifetime = max(lerp(g_sim.timing.y, g_sim.timing.z, random(seed)), 1e-3);
    ParticleState particle;
    if (g_sim.position.w > 0.5)
    {
        particle.positionAge = float4(g_sim.position.xyz + rotate(g_sim.rotation, offset * g_sim.scale.xyz), 0.0);
        particle.velocityLife = float4(rotate(g_sim.rotation, direction * speed), lifetime);
    }
    else
    {
        particle.positionAge = float4(offset, 0.0);
        particle.velocityLife = float4(direction * speed, lifetime);
    }
    g_output[slot] = particle;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void finishMain()
{
    const uint count = min(g_counters.Load(nextOffset()), g_sim.counts.y);
    g_counters.Store(nextOffset(), count);
    g_counters.Store(g_sim.state.x * 4, 0);
    g_arguments.Store4(0, uint4(6, count, 0, 0));
}
