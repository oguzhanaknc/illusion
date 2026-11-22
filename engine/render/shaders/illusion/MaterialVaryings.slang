// Vertex input and the varyings of the material passes (illusion/MaterialPass.slang), shared with the engine's
// fragment shaders that run with a material's vertex shader (MaterialPrepass.slang). Slang assigns locations in
// declaration order, so every stage that uses these structs agrees on them.

#pragma once

#include "../Common.slang"

// The engine's Vertex (48 bytes) in buffer slot 0 and the colour stream (RGBA8, white without vertex colours) in
// slot 1, in the order of the input layout of SceneRenderer.cpp.
struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION1;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentClip : TEXCOORD1;  // unjittered, for motion vectors
    float4 previousClip : TEXCOORD2; // previous frame
    nointerpolation float4 customData : CUSTOM;
    nointerpolation uint2 entity : ENTITY;
    nointerpolation uint material : MATERIAL;
    nointerpolation uint lod : LOD; // InstanceData::extra.w: level of detail and cross-fade
};

// The Entity's custom data: the fourth column of InstanceData::normalMatrix, which
// the shaders read only as a 3x3 matrix.
float4 instanceCustomData(InstanceData instance)
{
    return float4(instance.normalMatrix[0][3], instance.normalMatrix[1][3], instance.normalMatrix[2][3],
                  instance.normalMatrix[3][3]);
}

// Screen-space motion from the previous frame to this one, in UV units (origin top-left).
float2 motionVector(VertexOutput input)
{
    const float2 current = input.currentClip.xy / input.currentClip.w;
    const float2 previous = input.previousClip.xy / input.previousClip.w;
    return (current - previous) * float2(0.5, -0.5);
}
