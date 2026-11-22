// Fragment shaders that run with a material shader's vertex shader (ADR 0063) where the surface does not matter:
// the depth prepass of opaque surfaces that do not cross-fade (no discard, so depth tests stay early) and the
// wireframe overlay. They read the varyings of illusion/MaterialVaryings.slang.

#include "illusion/MaterialVaryings.slang"

// Depth prepass: depth plus motion vectors for TAA.
[shader("fragment")]
float2 prepassMain(VertexOutput input) : SV_Target
{
    return motionVector(input);
}

// Depth prepass with Entity IDs (editor picking) in a second target.
struct PrepassIds
{
    float2 motion : SV_Target0;
    uint2 entity : SV_Target1;
};

[shader("fragment")]
PrepassIds prepassIdsMain(VertexOutput input)
{
    PrepassIds output;
    output.motion = motionVector(input);
    output.entity = input.entity;
    return output;
}

[shader("fragment")]
float4 wireframeMain(VertexOutput input) : SV_Target0
{
    return float4(0.05, 0.8, 0.7, 1.0);
}
