// Two-phase GPU occlusion culling (after Haar and Aaltonen 2015). Candidates already passed the CPU
// frustum test. Phase 1 tests each one against last frame's Hi-Z with last frame's camera and draws the
// survivors; phase 2, after a Hi-Z of the phase-1 depth is built, re-tests only the rejected candidates
// with the current camera. Anything visible this frame is drawn in one of the phases, so nothing pops.
// Visible instances are appended to their batch's region of the draw list and counted in the batch's
// indirect draw arguments.

struct CullConstants
{
    float4x4 viewProjection;         // current, unjittered
    float4x4 previousViewProjection; // previous frame, unjittered
    float4 hiz;                      // xy: size of Hi-Z level 0, z: level count
    uint candidateCount;
    uint batchCount;
    uint phase2Offset; // start of the phase-2 region of the draw list
    uint flags;        // bit 0: occlusion culling on, bit 1: the previous Hi-Z is valid
};

struct Candidate
{
    float4 sphere; // world-space centre and radius
    uint batch;
    uint batchStart; // first draw-list slot of the batch
    uint2 padding;
};

struct CullPush
{
    uint phase; // 1 or 2
    uint padding0; // scalars: a uint3 would be aligned to 16 bytes
    uint padding1;
    uint padding2;
};

ConstantBuffer<CullConstants> g_cull : register(b0);
[[vk::push_constant]] ConstantBuffer<CullPush> g_push;
StructuredBuffer<Candidate> g_candidates : register(t0);
Texture2D<float> g_hiz : register(t1);
// Indirect arguments: 5 words per batch (index count, instance count, first index, vertex offset, first
// instance) for phase 1, then the same for phase 2, then one counter of drawn instances.
RWStructuredBuffer<uint> g_arguments : register(u0);
RWStructuredBuffer<uint> g_drawList : register(u1);
RWStructuredBuffer<uint> g_rejected : register(u2);

// True if the sphere is certainly hidden behind the depth in `g_hiz` as seen through `viewProjection`.
bool occluded(float4 sphere, float4x4 viewProjection)
{
    float2 lower = 1.0;
    float2 upper = 0.0;
    float closest = 0.0; // reversed-Z: larger is closer
    for (uint corner = 0; corner < 8; ++corner)
    {
        const float3 offset = float3((corner & 1u) != 0 ? 1.0 : -1.0, (corner & 2u) != 0 ? 1.0 : -1.0,
                                     (corner & 4u) != 0 ? 1.0 : -1.0);
        const float4 clip = mul(viewProjection, float4(sphere.xyz + offset * sphere.w, 1.0));
        if (clip.w <= 1e-5)
        {
            return false; // reaches the camera plane: cannot be bounded on screen
        }
        const float3 ndc = clip.xyz / clip.w;
        const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        lower = min(lower, uv);
        upper = max(upper, uv);
        closest = max(closest, ndc.z);
    }
    lower = saturate(lower);
    upper = saturate(upper);
    if (any(upper <= lower))
    {
        return false; // off screen for this camera: no information
    }
    // The level where the rectangle covers at most two texels in each direction.
    const float2 sizePixels = (upper - lower) * g_cull.hiz.xy;
    const float maxLevel = g_cull.hiz.z - 1.0;
    const uint level = uint(clamp(ceil(log2(max(max(sizePixels.x, sizePixels.y), 1.0))), 0.0, maxLevel));
    const int2 levelSize = max(int2(g_cull.hiz.xy) >> level, int2(1, 1));
    const int2 first = clamp(int2(lower * float2(levelSize)), int2(0, 0), levelSize - 1);
    const int2 last = clamp(int2(upper * float2(levelSize)), int2(0, 0), levelSize - 1);
    const float farthest =
        min(min(g_hiz.Load(int3(first, int(level))), g_hiz.Load(int3(int2(last.x, first.y), int(level)))),
            min(g_hiz.Load(int3(int2(first.x, last.y), int(level))), g_hiz.Load(int3(last, int(level)))));
    return closest < farthest;
}

void emit(uint candidate, Candidate data, uint phase)
{
    const uint argumentBase = (phase == 1 ? 0u : g_cull.batchCount * 5u) + data.batch * 5u;
    uint slot;
    InterlockedAdd(g_arguments[argumentBase + 1u], 1u, slot);
    const uint region = phase == 1 ? 0u : g_cull.phase2Offset;
    g_drawList[region + data.batchStart + slot] = candidate;
    uint unused;
    InterlockedAdd(g_arguments[g_cull.batchCount * 10u], 1u, unused);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void cullMain(uint3 id: SV_DispatchThreadID)
{
    const uint candidate = id.x;
    if (candidate >= g_cull.candidateCount)
    {
        return;
    }
    const Candidate data = g_candidates[candidate];
    const bool occlusion = (g_cull.flags & 1u) != 0u;
    if (g_push.phase == 1)
    {
        const bool test = occlusion && (g_cull.flags & 2u) != 0u;
        const bool hidden = test && occluded(data.sphere, g_cull.previousViewProjection);
        // Without a valid history, everything is drawn in phase 1 and nothing is re-tested.
        g_rejected[candidate] = hidden ? 1u : 0u;
        if (!hidden)
        {
            emit(candidate, data, 1);
        }
    }
    else if (occlusion && g_rejected[candidate] != 0u && !occluded(data.sphere, g_cull.viewProjection))
    {
        emit(candidate, data, 2);
    }
}
