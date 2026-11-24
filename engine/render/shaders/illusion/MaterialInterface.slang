// The interface check of material shaders (ADR 0063): ShaderCompiler compiles illusion/Surface.slang, the
// `.shader` source and this file first, for the reflection of Parameters (its layout and attributes) and to
// check that surface() exists with its signature. With ILLUSION_PROBE_WORLD_OFFSET it also calls
// worldOffset(), which tells whether the shader has one.

#include "Surface.slang"

// The view constants of the passes, for the surface API's helpers (windOffset()).
ConstantBuffer<ViewConstants> g_view : register(b0, space0);
ConstantBuffer<Parameters> illusionParameters : register(b0, space1);
Texture2D g_materialTextures[8] : register(t0, space1);
SamplerState g_materialSampler : register(s0, space1);

Parameters illusionMaterialParameters(uint material)
{
    return illusionParameters;
}

float4 illusionSampleTexture(uint material, Texture2DSlot texture, float2 uv)
{
    return g_materialTextures[min(texture, 7u)].Sample(g_materialSampler, uv);
}

float4 illusionSampleTextureLevel(uint material, Texture2DSlot texture, float2 uv, float level)
{
    return g_materialTextures[min(texture, 7u)].SampleLevel(g_materialSampler, uv, level);
}

[shader("fragment")]
float4 illusionInterface(float4 position: SV_Position) : SV_Target
{
    SurfaceInput input = (SurfaceInput)0;
    input.uv = position.xy;
    const SurfaceOutput output = surface(input);
    float4 result = float4(output.baseColor, output.opacity);
#if defined(ILLUSION_PROBE_WORLD_OFFSET)
    WorldOffsetInput offset = (WorldOffsetInput)0;
    offset.uv = position.xy;
    result.xyz += worldOffset(offset);
#endif
    return result;
}
