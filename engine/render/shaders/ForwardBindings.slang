// Bindings of the forward passes of material shaders (illusion/MaterialPass.slang): push constants, the view,
// instances and the frame's lighting resources in space 0. Must match the C++ layouts in SceneRenderer.cpp and
// RenderResources.cpp.

#pragma once

#include "Common.slang"

struct PushConstants
{
    uint instanceOffset;
    // bit 0: apply screen-space ambient occlusion (off for blended surfaces); bit 1: apply the fog; bit 2: a
    // skinned instance, whose previous positions start at previousVertex in g_previousPositions
    uint flags;
    uint previousVertex;
    uint padding;
};

static const uint kForwardSkinned = 4u;

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
StructuredBuffer<InstanceData> g_instances : register(t0, space0);
// Instance indices per draw: written by GPU culling for opaque batches, by the CPU for blended items.
StructuredBuffer<uint> g_drawList : register(t6, space0);
TextureCube g_specularCube : register(t1, space0);
Texture2D g_brdfLut : register(t2, space0);
Texture2D<float> g_ambientOcclusion : register(t5, space0);
SamplerState g_clampSampler : register(s0, space0);
// Integrated volumetric fog; opaque surfaces get it in VolumetricFog.slang, blended ones here.
Texture3D<float4> g_fogVolume : register(t8, space0);
// Model-space positions of skinned vertices under the previous frame's pose (Skinning.slang).
StructuredBuffer<float4> g_previousPositions : register(t10, space0);
[[vk::push_constant]] ConstantBuffer<PushConstants> g_push;

#include "FogSample.slang"
#include "ShadowReceiver.slang"
