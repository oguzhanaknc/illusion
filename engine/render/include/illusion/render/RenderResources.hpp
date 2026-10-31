#pragma once

/// @file
/// GPU-side meshes, textures, materials and lighting environments, referenced by small handles from ECS
/// components.
///
/// Uploads are synchronous (the call returns when the data is on the GPU); UploadQueue spreads models over
/// frames. Handles carry the generation of their slot: a released handle is stale at once, and its GPU
/// memory and slot are reused once the GPU finished the work submitted before the release (see
/// RenderResources::collect()). See docs/adr/0045-asset-catalog-and-file-watching.md.

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
    u32 index = 0;      ///< Slot (1-based; 0 = none).
    u32 generation = 0; ///< Use of the slot the handle was made for; a released handle is stale.
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const MeshHandle&, const MeshHandle&) noexcept = default;
};

/// Handle to a GPU texture.
struct TextureHandle
{
    u32 index = 0;      ///< Slot (1-based; 0 = none).
    u32 generation = 0; ///< Use of the slot the handle was made for; a released handle is stale.
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const TextureHandle&, const TextureHandle&) noexcept = default;
};

/// Handle to a material.
struct MaterialHandle
{
    u32 index = 0;      ///< Slot (1-based; 0 = the default material).
    u32 generation = 0; ///< Use of the slot the handle was made for; a released handle is stale.
    /// Handles are equal when they refer to the same slot.
    friend bool operator==(const MaterialHandle&, const MaterialHandle&) noexcept = default;
};

/// Handle to a lighting environment (see RenderResources::createEnvironment()).
struct EnvironmentHandle
{
    u32 index = 0;      ///< Slot (1-based; 0 = none).
    u32 generation = 0; ///< Use of the slot the handle was made for; a released handle is stale.
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

/// Memory use of the shared resources (see RenderResources::statistics()).
struct RenderResourceStatistics
{
    u32 vertices = 0;         ///< Vertices in use in the vertex pool.
    u32 vertexCapacity = 0;   ///< Vertices the vertex pool holds.
    u32 indices = 0;          ///< Indices in use in the index pool.
    u32 indexCapacity = 0;    ///< Indices the index pool holds.
    u32 freeRanges = 0;       ///< Free ranges inside the used part of both pools (fragmentation).
    u32 retiring = 0;         ///< Released resources waiting for the GPU before they are freed.
    u32 materialCapacity = 0; ///< Records the material table holds.
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

    /// Changes material @p material in place: the handle stays the same, the constants and the material
    /// table record are rewritten, and the binding set is made again only when a texture or the wrapping
    /// changed. The default material can be changed too. @p material must be valid (asserted).
    void updateMaterial(MaterialHandle material, const MaterialDesc& desc);

    /// Releases @p mesh. The handle is stale at once (handles to released resources draw nothing and
    /// assert when released or updated again); its ranges in the geometry pools are reused once the GPU
    /// finished the work submitted before this call. An empty handle is ignored.
    void releaseMesh(MeshHandle mesh);

    /// Releases @p texture like releaseMesh(). Materials that use it keep drawing it until they are
    /// updated or released, so update them first. The default textures cannot be released.
    void releaseTexture(TextureHandle texture);

    /// Releases @p material like releaseMesh(). The default material cannot be released (asserted).
    void releaseMaterial(MaterialHandle material);

    /// Releases @p environment like releaseMesh().
    void releaseEnvironment(EnvironmentHandle environment);

    /// Releases every mesh, texture and material of @p model.
    void releaseModel(const ModelHandles& model);

    /// Frees released resources whose GPU work has finished and returns how many. The create functions
    /// call it too; call it once per frame so memory is returned without waiting for the next upload.
    u32 collect();

    /// True if @p handle refers to a resource that was created and not released.
    [[nodiscard]] bool isValid(MeshHandle handle) const noexcept;
    /// @copydoc isValid(MeshHandle) const
    [[nodiscard]] bool isValid(TextureHandle handle) const noexcept;
    /// @copydoc isValid(MeshHandle) const
    [[nodiscard]] bool isValid(MaterialHandle handle) const noexcept;
    /// @copydoc isValid(MeshHandle) const
    [[nodiscard]] bool isValid(EnvironmentHandle handle) const noexcept;

    /// Diffuse irradiance of @p environment (zero for an invalid or released handle).
    [[nodiscard]] const ShIrradiance& irradiance(EnvironmentHandle environment) const noexcept;

    /// Number of submeshes of @p mesh (0 for an invalid or released handle).
    [[nodiscard]] u32 submeshCount(MeshHandle mesh) const noexcept;

    /// Bounds of submesh @p submesh of @p mesh in object space (empty for an invalid or released handle).
    [[nodiscard]] const Aabb& submeshBounds(MeshHandle mesh, u32 submesh) const noexcept;

    /// The default material (white, rough, non-metallic).
    [[nodiscard]] MaterialHandle defaultMaterial() const noexcept { return MaterialHandle{1}; }

    /// Number of meshes, textures, materials and environments currently stored (released ones are not
    /// counted; the textures include the two default textures, the materials the default material).
    [[nodiscard]] u32 meshCount() const noexcept;
    /// @copydoc meshCount
    [[nodiscard]] u32 textureCount() const noexcept;
    /// @copydoc meshCount
    [[nodiscard]] u32 materialCount() const noexcept;
    /// @copydoc meshCount
    [[nodiscard]] u32 environmentCount() const noexcept;

    /// Memory use of the geometry pools and resources waiting to be freed.
    [[nodiscard]] RenderResourceStatistics statistics() const noexcept;

    /// @cond INTERNAL
    struct GpuMesh;
    struct GpuMaterial;
    struct GpuEnvironment;
    [[nodiscard]] const GpuMesh* mesh(MeshHandle handle) const noexcept;
    // The default material for an invalid or released handle.
    [[nodiscard]] const GpuMaterial* material(MaterialHandle handle) const noexcept;
    // The binding set of @p handle's material (the default material's for an invalid or released handle).
    [[nodiscard]] nvrhi::IBindingSet* materialBindingSet(MaterialHandle handle) const noexcept;
    // The texture of @p handle, or nullptr for an invalid or released handle.
    [[nodiscard]] nvrhi::ITexture* texture(TextureHandle handle) const noexcept;
    [[nodiscard]] nvrhi::IBindingLayout* materialBindingLayout() const noexcept;
    /// The environment of @p handle, or a black environment for an invalid handle.
    [[nodiscard]] const GpuEnvironment& environment(EnvironmentHandle handle) const noexcept;
    [[nodiscard]] nvrhi::ITexture* brdfLut() const noexcept;
    [[nodiscard]] nvrhi::IBuffer* vertexPool() const noexcept;
    [[nodiscard]] nvrhi::IBuffer* indexPool() const noexcept;
    [[nodiscard]] nvrhi::IBuffer* materialTable() const noexcept;
    [[nodiscard]] nvrhi::ISampler* materialSampler(u32 index) const noexcept;
    [[nodiscard]] bool bindless() const noexcept;
    [[nodiscard]] nvrhi::IBindingLayout* bindlessLayout() const noexcept;
    [[nodiscard]] nvrhi::IDescriptorTable* bindlessTextures() const noexcept;
    // Increments whenever a pool or the material table is recreated or an environment slot changes owner
    // (binding sets must be rebuilt).
    [[nodiscard]] u32 generation() const noexcept;
    /// @endcond

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
