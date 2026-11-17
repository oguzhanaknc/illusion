// Skins the vertices of one instance for the frame (docs/adr/0059-skeletal-animation.md): reads its mesh's
// bind-pose Vertex range from the vertex pool and its SkinVertex range from the skin pool, blends the skin
// matrices of up to four joints, and writes the posed vertex (same 48-byte layout) plus the position under
// the previous frame's matrices (for motion vectors). Must match SkinningPush in SceneRenderer.cpp.

struct SkinningPush
{
    uint sourceVertex;     // first Vertex of the mesh in the vertex pool
    uint skinVertex;       // first SkinVertex of the mesh in the skin pool
    uint vertexCount;
    uint targetVertex;     // first vertex of the instance in the skinned vertex buffers
    uint matrices;         // first row (float4) of the instance's skin matrices, three per joint
    uint previousMatrices; // the same for the previous frame
    uint jointCount;
    uint padding;
};

static const uint kVertexBytes = 48;
static const uint kSkinVertexBytes = 16;

[[vk::push_constant]] ConstantBuffer<SkinningPush> g_push;
ByteAddressBuffer g_vertices : register(t0);
ByteAddressBuffer g_skin : register(t1);
StructuredBuffer<float4> g_matrices : register(t2);
RWByteAddressBuffer g_skinned : register(u0);
RWStructuredBuffer<float4> g_previous : register(u1);

// The weighted sum of the rows of the skin matrices of @p joints starting at row @p base.
void blend(uint base, uint4 joints, float4 weights, out float4 row0, out float4 row1, out float4 row2)
{
    row0 = float4(0.0);
    row1 = float4(0.0);
    row2 = float4(0.0);
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        const uint row = base + min(joints[i], g_push.jointCount - 1u) * 3u;
        row0 += weights[i] * g_matrices[row];
        row1 += weights[i] * g_matrices[row + 1u];
        row2 += weights[i] * g_matrices[row + 2u];
    }
}

float3 transformPoint(float4 row0, float4 row1, float4 row2, float3 p)
{
    const float4 h = float4(p, 1.0);
    return float3(dot(row0, h), dot(row1, h), dot(row2, h));
}

float3 transformDirection(float4 row0, float4 row1, float4 row2, float3 d)
{
    const float3 result = float3(dot(row0.xyz, d), dot(row1.xyz, d), dot(row2.xyz, d));
    const float length2 = dot(result, result);
    return length2 > 1e-20 ? result * rsqrt(length2) : d;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void skinMain(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= g_push.vertexCount)
    {
        return;
    }
    const uint source = (g_push.sourceVertex + id.x) * kVertexBytes;
    const float3 position = asfloat(g_vertices.Load3(source));
    const float3 normal = asfloat(g_vertices.Load3(source + 12u));
    const float4 tangent = asfloat(g_vertices.Load4(source + 24u));
    const uint2 uv = g_vertices.Load2(source + 40u);

    // Four u16 joint indices, then four unorm16 weights.
    const uint4 packed = g_skin.Load4((g_push.skinVertex + id.x) * kSkinVertexBytes);
    const uint4 joints = uint4(packed.x & 0xFFFFu, packed.x >> 16u, packed.y & 0xFFFFu, packed.y >> 16u);
    const float4 weights =
        float4(packed.z & 0xFFFFu, packed.z >> 16u, packed.w & 0xFFFFu, packed.w >> 16u) / 65535.0;

    float4 row0, row1, row2;
    blend(g_push.matrices, joints, weights, row0, row1, row2);
    const float3 skinnedPosition = transformPoint(row0, row1, row2, position);
    const float3 skinnedNormal = transformDirection(row0, row1, row2, normal);
    // A missing tangent (zero, w = 0) stays missing, so the shaders still derive one.
    const float3 skinnedTangent = transformDirection(row0, row1, row2, tangent.xyz);

    const uint target = (g_push.targetVertex + id.x) * kVertexBytes;
    g_skinned.Store3(target, asuint(skinnedPosition));
    g_skinned.Store3(target + 12u, asuint(skinnedNormal));
    g_skinned.Store4(target + 24u, asuint(float4(skinnedTangent, tangent.w)));
    g_skinned.Store2(target + 40u, uv);

    blend(g_push.previousMatrices, joints, weights, row0, row1, row2);
    g_previous[g_push.targetVertex + id.x] = float4(transformPoint(row0, row1, row2, position), 1.0);
}
