#pragma once

// Internal GPU representations shared by RenderResources and SceneRenderer.

#include <illusion/assets/Atmosphere.hpp>
#include <illusion/render/RenderResources.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace illusion
{

/// One material in the bindless material table (must match MaterialRecord in illusion/MaterialPass.slang):
/// its sampler, then its parameter block in the layout of its shader, with the bindless indices of its
/// textures.
struct GpuMaterialRecord
{
    u32 sampler = 0; // index into the sampler array (wrapU * 3 + wrapV)
    u32 padding[3]{};
    std::array<std::byte, kMaxShaderParameterBytes> parameters{};
};
static_assert(sizeof(GpuMaterialRecord) == 272);

// Meshes live in shared vertex and index pools; offsets are in vertices and indices.
struct RenderResources::GpuMesh
{
    u32 vertexOffset = 0;
    u32 indexOffset = 0;
    u32 vertexCount = 0; // size of the ranges in the pools
    u32 indexCount = 0;
    // Skinned meshes (ADR 0059): their SkinVertex stream in the skin pool, parallel to the vertices.
    u32 skinOffset = 0;
    u32 skinCount = 0; // 0 for a rigid mesh, else vertexCount
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
    u32 index = 0;                 // slot in the material table (equals the handle index)
    u32 shader = 1;                // slot of its shader (1: the standard shader)
    nvrhi::BufferHandle constants; // the parameter block of the bound passes
    nvrhi::BindingSetHandle bindingSet;
    std::array<u32, kMaxShaderTextures> textures{}; // texture slots it binds, by texture field
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    // Its vertices move (wind, worldOffset()): it draws with its own material in shadow maps too.
    bool moves = false;
};

// A material shader on the GPU: its layout and a vertex and fragment shader per variant that exists.
struct RenderResources::GpuShader
{
    ShaderAsset layout; // without SPIR-V
    // Changes with every createShader() and updateShader(), also when a slot is reused: renderers build again
    // the pipelines they made for another revision.
    u64 revision = 0;
    std::array<nvrhi::ShaderHandle, kShaderPassCount * 3> vertex{};
    std::array<nvrhi::ShaderHandle, kShaderPassCount * 3> fragment{};

    [[nodiscard]] static constexpr usize variantIndex(ShaderPass pass, AlphaMode mode) noexcept
    {
        return static_cast<usize>(pass) * 3 + static_cast<usize>(mode);
    }
    [[nodiscard]] nvrhi::IShader* vertexShader(ShaderPass pass, AlphaMode mode) const noexcept
    {
        return vertex[variantIndex(pass, mode)];
    }
    [[nodiscard]] nvrhi::IShader* fragmentShader(ShaderPass pass, AlphaMode mode) const noexcept
    {
        return fragment[variantIndex(pass, mode)];
    }
};

} // namespace illusion
