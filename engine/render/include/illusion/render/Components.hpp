#pragma once

/// @file
/// Runtime ECS Components read by the SceneRenderer (the saved ones are in SceneComponents.hpp), their
/// reflection, and spawning models as entities. Combine them with illusion::Transform.

#include <illusion/core/Math.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <string_view>
#include <vector>

namespace illusion
{

class World;
struct Entity;

/// What the SceneRenderer draws: one submesh of a GPU mesh with a material (runtime state, reflected as
/// "illusion.RenderMesh", TypeMeta::transient). A RenderAssetResolver writes it for every MeshRenderer;
/// procedural code writes it directly (leave sourceKey at 0 so no resolver touches it).
struct RenderMesh
{
    static constexpr u32 kVisible = 1u << 0;     ///< flags: drawn at all.
    static constexpr u32 kCastShadows = 1u << 1; ///< flags: drawn into shadow maps.

    MeshHandle mesh{};                   ///< Mesh to draw (none: nothing is drawn).
    u32 submesh = 0;                     ///< Submesh index.
    MaterialHandle material{};           ///< Material (0 = default material).
    u32 layers = 1;                      ///< Render layer bits (see MeshRenderer::layers).
    u32 flags = kVisible | kCastShadows; ///< kVisible, kCastShadows.
    u64 sourceKey = 0;                   ///< Key of the MeshRenderer it was resolved from; 0 for code.
};

/// The lighting environment of a SkyLight on the GPU (runtime state, reflected as "illusion.RenderSky",
/// TypeMeta::transient). A RenderAssetResolver writes it for a SkyLight with an environment asset; code that
/// bakes its own environment adds it next to a SkyLight without one.
struct RenderSky
{
    EnvironmentHandle environment{}; ///< Environment from RenderResources::createEnvironment().
    u64 sourceKey = 0;               ///< Key of the SkyLight it was resolved from; 0 for code.
};

/// Reflects the render Components of SceneComponents.hpp (MeshRenderer, Camera, DirectionalLight,
/// PointLight, SkyLight and ProceduralSky as "illusion.<Name>") with editor metadata, and RenderMesh,
/// RenderSky as runtime state (TypeMeta::transient). Also reflects the structs
/// ShadowSettings, PostProcessSettings (with AmbientOcclusionSettings, BloomSettings and
/// TemporalAntiAliasingSettings) and EnvironmentDesc as "illusion.<Name>" in registry.structs().
void reflectRenderComponents(ComponentRegistry& registry = ComponentRegistry::instance());

/// Creates the node hierarchy of @p model: one Entity per node with its Name, Hierarchy, LocalTransform and
/// Transform, placed relative to @p root (the model's roots become the World's last roots). A node with a
/// mesh of one submesh gets a MeshRenderer that refers to `<modelPath>#mesh/N` with the model's material
/// (an empty MeshRenderer::material); a mesh of several submeshes gets one child Entity per submesh, named
/// "<node> (<submesh>)". The entities are saved in scenes; a RenderAssetResolver loads the model. Returns
/// the created entities, parents before children.
std::vector<Entity> spawnModel(World& world, const ModelData& model, std::string_view modelPath,
                               const Mat4& root = Mat4(1.0f));

/// Like spawnModel() with a path, for a model uploaded by code: the entities get RenderMesh Components with
/// the GPU @p handles instead of MeshRenderers, so they are drawn at once but not saved in scenes.
std::vector<Entity> spawnModel(World& world, const ModelData& model, const ModelHandles& handles,
                               const Mat4& root = Mat4(1.0f));

} // namespace illusion
