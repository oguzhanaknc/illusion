// The decal API (ADR 0066), included for decal shaders. A decal shader is a material shader whose Parameters
// carry [Decal("...")] with the channels it writes, separated by spaces:
//
//     [Decal("BaseColor Normal ORM Emission")] // at least one; no [ShadingModel], no worldOffset()
//     struct Parameters { ... };
//     SurfaceOutput surface(SurfaceInput input);
//
// Its surface() runs for every pixel of a receiving surface inside the decal's box (illusion/DecalPass.slang):
// worldPosition and normal are the receiver's, reconstructed from depth; tangent and bitangent are the box's x and
// y axes projected onto the receiver; uv is the box's x and y mapped to [0, 1] with v down; color is the decal's
// colour and customData is (age in seconds, life fraction from 0 to 1, fade weight, variant). Of SurfaceOutput the
// decal uses baseColor, normal (tangent space), occlusion, roughness, metallic and emission, each weighted by
// opacity; the other fields are ignored.

#pragma once

#include "Surface.slang"

[__AttributeUsage(_AttributeTargets.Struct)]
struct DecalAttribute
{
    string channels;
};

// Seconds since the decal was spawned (0 for decals placed in the scene).
float decalAge(SurfaceInput input)
{
    return input.customData.x;
}

// How far the decal is through its lifetime, from 0 to 1 (0 for decals without one).
float decalLife(SurfaceInput input)
{
    return input.customData.y;
}

// The decal's variant (Decal::variant): an atlas frame or another variation.
uint decalVariant(SurfaceInput input)
{
    return uint(max(input.customData.w, 0.0) + 0.5);
}

// @p uv moved into frame @p frame of an atlas of @p columns by @p rows frames, numbered row by row from the top
// left and wrapping around the frame count.
float2 decalAtlasUv(float2 uv, uint frame, uint columns, uint rows)
{
    const uint across = max(columns, 1u);
    const uint down = max(rows, 1u);
    const uint cell = frame % (across * down);
    return (float2(float(cell % across), float(cell / across)) + uv) / float2(float(across), float(down));
}
