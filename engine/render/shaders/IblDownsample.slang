// One cube map mip from the previous one (2x2 box filter).

#include "IblCommon.slang"

Texture2DArray g_sourceMip : register(t0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> g_outputCube : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void downsampleCube(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= g_params.size))
    {
        return;
    }
    const int2 source = int2(id.xy) * 2;
    const int slice = int(id.z);
    const float4 sum = g_sourceMip.Load(int4(source, slice, 0)) + g_sourceMip.Load(int4(source + int2(1, 0), slice, 0)) +
                       g_sourceMip.Load(int4(source + int2(0, 1), slice, 0)) +
                       g_sourceMip.Load(int4(source + int2(1, 1), slice, 0));
    g_outputCube[id] = sum * 0.25;
}

// GGX prefiltering with filtered importance sampling (Krivanek and Colbert 2008), assuming n = v = r.
