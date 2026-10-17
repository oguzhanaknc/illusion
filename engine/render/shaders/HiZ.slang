// Hierarchical depth for occlusion culling: each texel of a level holds the farthest depth (the smallest
// value with reversed-Z) of the texels it covers in the level below, so a test against one texel is
// conservative. Odd source sizes fold the extra row or column into the last output texel.

Texture2D<float> g_source : register(t0); // the depth buffer or the previous Hi-Z level
[[vk::image_format("r32f")]] RWTexture2D<float> g_output : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void hizReduceMain(uint3 id: SV_DispatchThreadID)
{
    uint width;
    uint height;
    g_output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    uint sourceWidth;
    uint sourceHeight;
    g_source.GetDimensions(sourceWidth, sourceHeight);
    const int2 last = int2(sourceWidth, sourceHeight) - 1;
    const int2 base = int2(id.xy) * 2;
    // Include a third texel when this is the last output texel of an odd-sized source.
    const int2 extent = int2(id.x + 1 == width && (sourceWidth & 1u) != 0 ? 3 : 2,
                             id.y + 1 == height && (sourceHeight & 1u) != 0 ? 3 : 2);
    float farthest = 1.0;
    for (int y = 0; y < extent.y; ++y)
    {
        for (int x = 0; x < extent.x; ++x)
        {
            farthest = min(farthest, g_source.Load(int3(min(base + int2(x, y), last), 0)));
        }
    }
    g_output[id.xy] = farthest;
}
