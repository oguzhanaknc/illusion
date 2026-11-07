// Lookup of the integrated volumetric fog (VolumetricFog, ADR 0050), shared by the fog's apply pass, blended
// surfaces (ForwardPbr.slang) and particles (ParticlesCommon.slang). The volume is a froxel grid aligned with
// the screen whose slices are spaced exponentially in view depth between a near and a far distance; texel z
// holds the light scattered towards the camera (rgb) and the transmittance (a) from the camera to the far
// side of slice z. Layouts of `params` must match SceneRenderer.cpp.

// Fog over a point at @p viewDepth (along the camera's forward axis) behind screen position @p uv (origin
// top-left). @p params: x 1 with fog, y near distance, z slice count / log(far / near), w slice count. Returns
// in-scattered light (rgb) and transmittance (a): fogged = color * a + rgb.
float4 sampleFog(Texture3D<float4> volume, SamplerState linearClamp, float4 params, float2 uv, float viewDepth)
{
    if (params.x <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    // Continuous slice boundary: 0 at the near distance, the slice count at the far one.
    const float slice = min(log(max(viewDepth, 1e-6) / params.y) * params.z, params.w);
    if (slice <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (slice < 1.0)
    {
        const float4 first = volume.SampleLevel(linearClamp, float3(uv, 0.5 / params.w), 0.0);
        return lerp(float4(0.0, 0.0, 0.0, 1.0), first, slice);
    }
    // Boundary s is the far side of slice s - 1, whose texel centre is at (s - 0.5) / count.
    return volume.SampleLevel(linearClamp, float3(uv, (slice - 0.5) / params.w), 0.0);
}
