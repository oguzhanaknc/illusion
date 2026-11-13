#pragma once

// Internal GPU representations shared by RenderResources and SceneRenderer.

#include <illusion/assets/Atmosphere.hpp>
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
    f32 alphaCutoff;   // < 0 disables masking
    Vec4 wind;         // x: sway, y: flutter (MaterialData::windSway, windFlutter)
    Vec4 transmission; // rgb: diffuseTransmission * diffuseTransmissionColor, w: diffuseTransmission
};
static_assert(sizeof(GpuMaterialConstants) == 80);

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
static_assert(sizeof(GpuMaterialRecord) == 112);

// Meshes live in shared vertex and index pools; offsets are in vertices and indices.
struct RenderResources::GpuMesh
{
    u32 vertexOffset = 0;
    u32 indexOffset = 0;
    u32 vertexCount = 0; // size of the ranges in the pools
    u32 indexCount = 0;
    std::vector<Submesh> submeshes;
};

struct RenderResources::GpuEnvironment
{
    nvrhi::TextureHandle background; // RGBA16F or BC6H cube map with a full mip chain
    nvrhi::TextureHandle specular; // RGBA16F or BC6H cube map, mip i prefiltered for roughness i / (mips - 1)
    u32 specularMips = 1;
    ShIrradiance irradiance;
    u64 bytes = 0;             // of both cube maps on the GPU
    u64 uncompressedBytes = 0; // of both cube maps as RGBA16F
    // Atmosphere environments (createAtmosphereEnvironment()) are redrawn in place with these: the sky
    // constants and the binding sets of the sky, downsample and prefilter passes, made once.
    nvrhi::BufferHandle atmosphereConstants;
    std::vector<nvrhi::BindingSetHandle> atmosphereSets;
};

/// GPU layout of an AtmosphereDesc (must match AtmosphereData in Atmosphere.slang).
struct GpuAtmosphere
{
    Vec4 sun;    // xyz: towards the sun, w: sun radiance scale
    Vec4 moon;   // xyz: towards the moon, w: moon radiance scale
    Vec4 params; // x: turbidity
    Vec4 ground; // rgb: ground albedo
};
static_assert(sizeof(GpuAtmosphere) == 64);

/// Packs @p desc for the shaders, with normalized directions.
[[nodiscard]] GpuAtmosphere packAtmosphere(const AtmosphereDesc& desc) noexcept;

struct RenderResources::GpuMaterial
{
    u32 index = 0; // slot in the material table (equals the handle index)
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindingSet;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    bool wind = false; // moves in the wind: its vertices depend on the material, also in shadow maps
};

} // namespace illusion
