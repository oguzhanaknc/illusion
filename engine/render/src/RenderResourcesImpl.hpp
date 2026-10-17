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

/// One material in the bindless material table (must match MaterialRecord in ForwardPbr.slang).
struct GpuMaterialRecord
{
    GpuMaterialConstants constants;
    u32 baseColorTexture; // indices into the bindless texture table
    u32 metallicRoughnessTexture;
    u32 normalTexture;
    u32 occlusionTexture;
    u32 emissiveTexture;
    u32 sampler; // index into the sampler array (wrapU * 3 + wrapV)
    u32 padding[2];
};
static_assert(sizeof(GpuMaterialRecord) == 80);

// Meshes live in shared vertex and index pools; offsets are in vertices and indices.
struct RenderResources::GpuMesh
{
    u32 vertexOffset = 0;
    u32 indexOffset = 0;
    std::vector<Submesh> submeshes;
};

struct RenderResources::GpuEnvironment
{
    nvrhi::TextureHandle background; // RGBA16F cube map with a full mip chain
    nvrhi::TextureHandle specular;   // RGBA16F cube map, mip i prefiltered for roughness i / (mips - 1)
    u32 specularMips = 1;
    ShIrradiance irradiance;
};

struct RenderResources::GpuMaterial
{
    u32 index = 0; // slot in the material table (equals the handle index)
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindingSet;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
};

} // namespace illusion
