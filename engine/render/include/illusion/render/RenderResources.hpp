#pragma once

/// @file
/// GPU-side meshes, textures, materials and lighting environments, referenced by small handles from ECS
/// components.
///
/// Uploads are synchronous for now (the call returns when the data is on the GPU); asynchronous
/// streaming arrives with the asset pipeline milestone.

#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/Base.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>
#include <span>
#include <vector>

namespace illusion
{

class RenderDevice;

/// Handle to a GPU mesh. Index 0 is never valid.
struct MeshHandle
{
    u32 index = 0; ///< Slot (1-based; 0 = none).
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const MeshHandle&, const MeshHandle&) noexcept = default;
};

/// Handle to a GPU texture.
struct TextureHandle
{
    u32 index = 0; ///< Slot (1-based; 0 = none).
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const TextureHandle&, const TextureHandle&) noexcept = default;
};

/// Handle to a material.
struct MaterialHandle
{
    u32 index = 0; ///< Slot (1-based; 0 = the default material).
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const MaterialHandle&, const MaterialHandle&) noexcept = default;
};

/// Handle to a lighting environment (see RenderResources::createEnvironment()).
struct EnvironmentHandle
{
    u32 index = 0; ///< Slot (1-based; 0 = none).
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const EnvironmentHandle&, const EnvironmentHandle&) noexcept = default;
};

/// Resolution of the cube maps baked by RenderResources::createEnvironment().
struct EnvironmentDesc
{
    u32 cubeSize = 512; ///< Face size of the background cube map (rounded down to a power of two).
    u32 specularSize =
        256; ///< Face size of the prefiltered specular cube map (rounded down to a power of two).
    u32 sampleCount = 128; ///< GGX samples per texel when prefiltering (at least 1).
};

/// Material parameters with GPU textures (unset textures use neutral defaults).
struct MaterialDesc
{
    MaterialData parameters;         ///< Factors, alpha mode, wrapping; texture image indices are ignored.
    TextureHandle baseColor;         ///< sRGB base colour texture.
    TextureHandle metallicRoughness; ///< Roughness (G) and metalness (B).
    TextureHandle normal;            ///< Tangent-space normal map.
    TextureHandle occlusion;         ///< Occlusion (R).
    TextureHandle emissive;          ///< sRGB emissive texture.
};

/// Handles created by uploading a ModelData, indexed like its meshes, images and materials.
struct ModelHandles
{
    std::vector<MeshHandle> meshes;        ///< One per ModelData mesh.
    std::vector<TextureHandle> textures;   ///< One per ModelData image (unset for unused images).
    std::vector<MaterialHandle> materials; ///< One per ModelData material.
};

/// Owns GPU resources. Not thread-safe; use from the render thread.
class RenderResources : NonCopyable
{
public:
    /// Creates the resource store and its default textures and material.
    explicit RenderResources(RenderDevice& device);
    ~RenderResources();

    /// Uploads a mesh.
    [[nodiscard]] MeshHandle createMesh(const MeshData& mesh, const char* debugName = "Mesh");

    /// Uploads an image with all its mips.
    [[nodiscard]] TextureHandle createTexture(const Image& image, const char* debugName = "Texture");

    /// Creates a material.
    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& desc);

    /// Uploads every mesh, used image and material of @p model.
    [[nodiscard]] ModelHandles uploadModel(const ModelData& model);

    /// Bakes a lighting environment from an equirectangular image (any format; use RGBA32F from an HDR
    /// file for real lighting): a background cube map, a GGX-prefiltered specular cube map and diffuse
    /// irradiance as spherical harmonics. Runs on the GPU and returns when the bake is done.
    [[nodiscard]] EnvironmentHandle createEnvironment(const Image& equirect, const EnvironmentDesc& desc = {},
                                                      const char* debugName = "Environment");

    /// Diffuse irradiance of @p environment (zero for an invalid handle).
    [[nodiscard]] const ShIrradiance& irradiance(EnvironmentHandle environment) const noexcept;

    /// Number of submeshes of @p mesh (0 for an invalid handle).
    [[nodiscard]] u32 submeshCount(MeshHandle mesh) const noexcept;

    /// Bounds of submesh @p submesh of @p mesh in object space.
    [[nodiscard]] const Aabb& submeshBounds(MeshHandle mesh, u32 submesh) const noexcept;

    /// The default material (white, rough, non-metallic).
    [[nodiscard]] MaterialHandle defaultMaterial() const noexcept { return MaterialHandle{1}; }

    /// Number of meshes, textures and materials currently stored.
    [[nodiscard]] u32 meshCount() const noexcept;
    /// @copydoc meshCount
    [[nodiscard]] u32 textureCount() const noexcept;
    /// @copydoc meshCount
    [[nodiscard]] u32 materialCount() const noexcept;
    /// @copydoc meshCount
    [[nodiscard]] u32 environmentCount() const noexcept;

    /// @cond INTERNAL
    struct GpuMesh;
    struct GpuMaterial;
    struct GpuEnvironment;
    [[nodiscard]] const GpuMesh* mesh(MeshHandle handle) const noexcept;
    [[nodiscard]] const GpuMaterial* material(MaterialHandle handle) const noexcept;
    [[nodiscard]] nvrhi::IBindingLayout* materialBindingLayout() const noexcept;
    /// The environment of @p handle, or a black environment for an invalid handle.
    [[nodiscard]] const GpuEnvironment& environment(EnvironmentHandle handle) const noexcept;
    [[nodiscard]] nvrhi::ITexture* brdfLut() const noexcept;
    /// @endcond

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
