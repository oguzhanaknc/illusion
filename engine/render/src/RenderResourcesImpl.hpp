#pragma once

// Internal GPU representations shared by RenderResources and SceneRenderer.

#include <illusion/render/RenderResources.hpp>

#include <vector>

namespace illusion
{

/// GPU layout of a material's constants (must match MaterialConstants in ForwardPbr.slang).
struct GpuMaterialConstants
{
    Vec4 baseColorFactor;
    Vec4 emissiveFactor; // w: normal scale
    f32 metallicFactor;
    f32 roughnessFactor;
    f32 occlusionStrength;
    f32 alphaCutoff; // < 0 disables masking
};
static_assert(sizeof(GpuMaterialConstants) == 48);

struct RenderResources::GpuMesh
{
    nvrhi::BufferHandle vertexBuffer;
    nvrhi::BufferHandle indexBuffer;
    std::vector<Submesh> submeshes;
};

struct RenderResources::GpuMaterial
{
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindingSet;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
};

} // namespace illusion
