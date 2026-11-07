// Shared by Particles.slang (CPU-simulated particles) and ParticlesGpu.slang (GPU-simulated ones): each live
// particle is a camera-facing quad of two triangles, drawn instanced into the HDR target after the blended
// geometry (ADR 0047). Colours are premultiplied by alpha, so additive particles use One/One blending and
// alpha particles One/InverseSourceAlpha. The scene depth is read, not attached: a particle behind the scene
// is discarded and one near it fades out (soft particles). Volumetric fog in front of a particle dims it, and
// alpha particles take on the fog's glow (FogSample.slang). Layouts must match ParticleRenderer.cpp.

#include "FogSample.slang"

struct ParticleConstants
{
    float4x4 viewProjection;    // jittered like the scene, so particles stay put under TAA
    float4x4 inverseProjection; // clip -> view, to turn scene depth into a view distance
    float4 cameraRight;         // xyz
    float4 cameraUp;            // xyz
    float4 cameraForward;       // xyz
    float4 cameraPosition;      // xyz
    float4 params;              // x: 1 / soft distance (0: a hard edge)
    float4 fog;                 // ViewConstants::fog
    float4 fogScreen;           // ViewConstants::fogScreen
};

struct ParticlePush
{
    uint firstInstance;
    uint textured; // 1: sample the sprite; 0: a soft round dot
    uint additive; // 1: additive blending (the fog dims the particle but adds no glow of its own)
    uint padding1;
    // GPU-simulated emitters only: the look over life and the placement of local-space particles.
    float4 colorStart;
    float4 colorEnd;
    float4 size;          // x: width at birth, y: width at death
    float4 modelPosition; // xyz; w: 1 for local-space particles
    float4 modelRotation; // quaternion xyzw
    float4 modelScale;    // xyz
};

ConstantBuffer<ParticleConstants> g_particles : register(b0, space0);
Texture2D g_sprite : register(t1, space0);
Texture2D<float> g_depth : register(t2, space0); // the scene depth (reversed-Z)
Texture3D<float4> g_fogVolume : register(t3, space0); // integrated volumetric fog
SamplerState g_sampler : register(s0, space0);
[[vk::push_constant]] ConstantBuffer<ParticlePush> g_push;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 corner : TEXCOORD0;   // -1..1 across the quad
    float4 color : COLOR0;
    float viewDepth : TEXCOORD1; // distance along the camera's forward axis
};

static const float2 kCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                    float2(-1, -1), float2(1, 1), float2(-1, 1) };

// Corner @p vertexId of the camera-facing quad of a particle at @p world, @p width wide.
VertexOutput billboard(uint vertexId, float3 world, float width, float4 color)
{
    const float2 corner = kCorners[vertexId];
    const float halfSize = width * 0.5;
    const float3 placed = world + (g_particles.cameraRight.xyz * corner.x + g_particles.cameraUp.xyz * corner.y) * halfSize;
    VertexOutput output;
    output.position = mul(g_particles.viewProjection, float4(placed, 1.0));
    output.corner = corner;
    output.color = color;
    output.viewDepth = dot(placed - g_particles.cameraPosition.xyz, g_particles.cameraForward.xyz);
    return output;
}

// Distance along the camera's forward axis of the scene surface behind pixel @p pixel.
float sceneViewDepth(int2 pixel)
{
    const float depth = g_depth.Load(int3(pixel, 0));
    if (depth <= 0.0)
    {
        return 1e30; // reversed-Z: nothing was drawn here
    }
    const float4 view = mul(g_particles.inverseProjection, float4(0.0, 0.0, depth, 1.0));
    return -view.z / view.w;
}

float4 shadeParticle(VertexOutput input)
{
    float4 color = input.color;
    if (g_push.textured != 0u)
    {
        color *= g_sprite.Sample(g_sampler, input.corner * float2(0.5, -0.5) + 0.5);
    }
    else
    {
        // A soft round dot: full in the middle, nothing at the edge of the quad.
        const float radius = length(input.corner);
        const float falloff = saturate(1.0 - radius);
        color.a *= saturate(falloff * falloff * 2.0);
    }
    // The depth test: nothing behind the scene, and a soft fade over the last stretch before it.
    const float gap = sceneViewDepth(int2(input.position.xy)) - input.viewDepth;
    if (gap <= 0.0)
    {
        discard;
    }
    if (g_particles.params.x > 0.0)
    {
        color.a *= saturate(gap * g_particles.params.x);
    }
    if (color.a <= 0.0)
    {
        discard;
    }
    float3 premultiplied = color.rgb * color.a;
    const float4 fog = sampleFog(g_fogVolume, g_sampler, g_particles.fog,
                                 input.position.xy * g_particles.fogScreen.xy, input.viewDepth);
    premultiplied = premultiplied * fog.a + (g_push.additive != 0u ? float3(0.0, 0.0, 0.0) : fog.rgb * color.a);
    return float4(premultiplied, color.a);
}
