// Selection outline of the editor viewports, drawn after tonemapping: a full-screen triangle that colours
// the pixels near (but outside) the selected entities' visible surfaces. The selection is a bitset indexed
// by Entity index; the renderer's Entity ID target says which entity each pixel shows.

struct Constants
{
    float4 sizes;  // render xy, output zw
    float4 color;  // outline colour
    uint4 options; // x: selection words, y: radius in render pixels
};

ConstantBuffer<Constants> g_outline : register(b0);
Texture2D<uint2> g_ids : register(t0);
StructuredBuffer<uint> g_selection : register(t1);

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 clip = float2(float((vertexId << 1) & 2), float(vertexId & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    return output;
}

bool selectedAt(int2 pixel)
{
    const int2 size = int2(g_outline.sizes.xy);
    if (any(pixel < int2(0)) || any(pixel >= size))
    {
        return false;
    }
    const uint index = g_ids.Load(int3(pixel, 0)).x;
    const uint word = index >> 5;
    return index != 0xFFFFFFFFu && word < g_outline.options.x && ((g_selection[word] >> (index & 31u)) & 1u) != 0u;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy * g_outline.sizes.xy / g_outline.sizes.zw);
    if (selectedAt(pixel))
    {
        discard;
    }
    const int radius = int(g_outline.options.y);
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            if (x * x + y * y <= radius * radius + radius && selectedAt(pixel + int2(x, y)))
            {
                return g_outline.color;
            }
        }
    }
    discard;
    return float4(0.0);
}
