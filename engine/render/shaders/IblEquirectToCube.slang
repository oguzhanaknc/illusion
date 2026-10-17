// Equirectangular image to cube map (mip 0).

#include "IblCommon.slang"

Texture2D g_equirect : register(t0);
SamplerState g_sampler : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> g_outputCube : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void equirectToCube(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= g_params.size))
    {
        return;
    }
    const float3 direction = cubeDirection(id.xy, id.z, g_params.size);
    g_outputCube[id] = float4(g_equirect.SampleLevel(g_sampler, equirectUv(direction), 0.0).rgb, 1.0);
}
