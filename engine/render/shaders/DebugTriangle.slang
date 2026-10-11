// A single coloured triangle drawn without vertex buffers. Used by GPU smoke tests and the
// hello_triangle sample to verify the whole path from shader compilation to presentation.

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

// Clip space is y-up (NVRHI flips the Vulkan viewport): top, bottom right, bottom left.
static const float2 kPositions[3] = { float2(0.0, 0.6), float2(0.6, -0.6), float2(-0.6, -0.6) };
static const float3 kColors[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    VertexOutput output;
    output.position = float4(kPositions[vertexId], 0.0, 1.0);
    output.color = kColors[vertexId];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
