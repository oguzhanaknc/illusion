// Draws a SkyAtmosphere into mip 0 of its environment's background cube map; IblDownsample and IblPrefilter
// then update the reflections in place (RenderResources::updateAtmosphereEnvironment()).

#include "IblCommon.slang"
#include "Atmosphere.slang"

ConstantBuffer<AtmosphereData> g_atmosphere : register(b0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> g_outputCube : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void atmosphereCube(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= g_params.size))
    {
        return;
    }
    const float3 direction = cubeDirection(id.xy, id.z, g_params.size);
    g_outputCube[id] = float4(min(atmosphereRadiance(g_atmosphere, direction), 65504.0), 1.0);
}
