#pragma once

/// @file
/// CPU-side models (meshes, PBR materials, images, node hierarchy) and glTF 2.0 import.

#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class VirtualFileSystem;

/// How a material's alpha is used.
enum class AlphaMode : u8
{
    Opaque, ///< Alpha is ignored.
    Mask,   ///< Pixels with alpha below MaterialData::alphaCutoff are discarded.
    Blend,  ///< Alpha blending (rendered after opaque objects).
};

/// Texture addressing outside [0, 1].
enum class TextureWrap : u8
{
    Repeat,       ///< Tile.
    ClampToEdge,  ///< Clamp.
    MirrorRepeat, ///< Tile with mirroring.
};

/// A texture used by a material.
struct TextureRef
{
    i32 image = -1;                          ///< Index into ModelData::images, or -1 for none.
    TextureWrap wrapU = TextureWrap::Repeat; ///< Horizontal wrapping.
    TextureWrap wrapV = TextureWrap::Repeat; ///< Vertical wrapping.

    /// Returns true if a texture is assigned.
    [[nodiscard]] bool isSet() const noexcept { return image >= 0; }
};

/// Physically based metallic-roughness material (the glTF 2.0 model).
struct MaterialData
{
    std::string name;                        ///< Material name.
    Vec4 baseColorFactor{1.0f};              ///< Linear base colour multiplier.
    f32 metallicFactor = 1.0f;               ///< Metalness multiplier.
    f32 roughnessFactor = 1.0f;              ///< Perceptual roughness multiplier.
    Vec3 emissiveFactor{0.0f};               ///< Linear emissive colour.
    f32 normalScale = 1.0f;                  ///< Normal map strength.
    f32 occlusionStrength = 1.0f;            ///< Ambient occlusion strength.
    f32 alphaCutoff = 0.5f;                  ///< Mask threshold.
    AlphaMode alphaMode = AlphaMode::Opaque; ///< Alpha usage.
    bool doubleSided = false;                ///< Disables back-face culling.
    TextureRef baseColorTexture;             ///< sRGB base colour (+ alpha).
    TextureRef metallicRoughnessTexture;     ///< Linear: roughness in G, metalness in B.
    TextureRef normalTexture;                ///< Tangent-space normal map.
    TextureRef occlusionTexture;             ///< Linear: occlusion in R.
    TextureRef emissiveTexture;              ///< sRGB emissive colour.
};

/// A node of the model hierarchy.
struct ModelNode
{
    std::string name;       ///< Node name.
    Mat4 localMatrix{1.0f}; ///< Transform relative to the parent.
    i32 parent = -1;        ///< Parent node index, or -1 for a root.
    i32 mesh = -1;          ///< Mesh index, or -1.
};

/// A model: everything a glTF file describes that the engine renders.
struct ModelData
{
    std::vector<MeshData> meshes;        ///< Meshes.
    std::vector<MaterialData> materials; ///< Materials.
    std::vector<Image> images;           ///< Decoded images with full mip chains.
    std::vector<ModelNode> nodes;        ///< Nodes, parents before children.

    /// World matrix of every node (parent chain applied), indexed like `nodes`.
    [[nodiscard]] std::vector<Mat4> worldMatrices() const;

    /// Bounds of all mesh instances in model space.
    [[nodiscard]] Aabb bounds() const;
};

/// Loads a glTF 2.0 file (.gltf with external or embedded buffers, or binary .glb) through @p files.
/// Supports triangle meshes, metallic-roughness materials, PNG and JPEG textures, and the node
/// hierarchy of the default scene. Unsupported required extensions (Draco, meshopt, KTX2) are errors.
/// Returns std::nullopt and sets @p error on failure. When @p dependencies is given, it receives the
/// normalized virtual paths of every file read (the model, external buffers and images).
[[nodiscard]] std::optional<ModelData> loadGltf(const VirtualFileSystem& files, std::string_view virtualPath,
                                                std::string* error = nullptr,
                                                std::vector<std::string>* dependencies = nullptr);

} // namespace illusion
