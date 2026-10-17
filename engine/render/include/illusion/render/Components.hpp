#pragma once

/// @file
/// ECS components read by the SceneRenderer. Combine them with illusion::Transform.

#include <illusion/core/Math.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/render/RenderResources.hpp>

#include <vector>

namespace illusion
{

class World;
struct Entity;

/// Draws one submesh of a mesh with a material.
struct MeshRenderer
{
    MeshHandle mesh;         ///< Mesh to draw.
    u32 submesh = 0;         ///< Submesh index.
    MaterialHandle material; ///< Material (0 = default material).
};

/// Perspective camera. The renderer looks through the Transform's -z axis.
struct Camera
{
    f32 fovY = 1.0471976f; ///< Vertical field of view in radians (60 degrees).
    f32 nearPlane = 0.1f;  ///< Near clip distance. There is no far plane (reversed-Z, infinite).
    f32 exposure = 1.0f;   ///< Linear exposure multiplier applied before tonemapping.
};

/// Sun-like light. It shines along the Transform's -z axis.
struct DirectionalLight
{
    Vec3 color{1.0f};        ///< Linear colour.
    f32 intensity = 3.0f;    ///< Illuminance multiplier.
    bool castShadows = true; ///< The first DirectionalLight with this set casts shadows.
    f32 angularDiameter =
        0.0093f; ///< Apparent size in radians (the sun: 0.0093); larger gives softer shadows.
};

/// Omnidirectional light at the Transform's position.
struct PointLight
{
    Vec3 color{1.0f};      ///< Linear colour.
    f32 intensity = 10.0f; ///< Luminous intensity multiplier.
    f32 range = 10.0f;     ///< Distance at which the light fades to zero.
};

/// Image-based lighting and background from an environment. The renderer uses the first SkyLight it
/// finds; the Transform's rotation turns the environment (position and scale are ignored).
struct SkyLight
{
    EnvironmentHandle environment; ///< Environment from RenderResources::createEnvironment().
    f32 intensity = 1.0f;          ///< Multiplier for the environment's radiance.
    bool background = true;        ///< Draw the environment behind the scene instead of the clear colour.
};

/// World matrix of a MeshRenderer entity in the previous frame, for motion vectors (temporal
/// anti-aliasing). The SceneRenderer adds and updates it; set it to the current matrix after teleporting
/// an object so the jump is not treated as motion.
struct MotionHistory
{
    Mat4 previousWorld{1.0f}; ///< World matrix used by the previous render() call.
};

/// Reflects the render components that hold plain data: Camera ("illusion.Camera"), DirectionalLight
/// ("illusion.DirectionalLight") and PointLight ("illusion.PointLight"). MeshRenderer and SkyLight refer to
/// GPU resources and are not saved in scenes.
void reflectRenderComponents(ComponentRegistry& registry = ComponentRegistry::instance());

/// Creates one Entity per mesh-bearing node and submesh of @p model (Transform + MeshRenderer), placed
/// relative to @p root. Returns the created entities.
std::vector<Entity> spawnModel(World& world, const ModelData& model, const ModelHandles& handles,
                               const Mat4& root = Mat4(1.0f));

} // namespace illusion
