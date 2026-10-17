#pragma once

/// @file
/// GPU-side meshes, textures and materials, referenced by small handles from ECS components.
///
/// Uploads are synchronous for now (the call returns when the data is on the GPU); asynchronous
/// streaming arrives with the asset pipeline milestone.

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

    /// @cond INTERNAL
    struct GpuMesh;
    struct GpuMaterial;
    [[nodiscard]] const GpuMesh* mesh(MeshHandle handle) const noexcept;
    [[nodiscard]] const GpuMaterial* material(MaterialHandle handle) const noexcept;
    [[nodiscard]] nvrhi::IBindingLayout* materialBindingLayout() const noexcept;
    /// @endcond

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
