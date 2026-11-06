#pragma once

/// @file
/// What `transform.drop_to_ground` with `"onto": "colliders"` needs from physics: how far entities fall
/// onto the Colliders of the edited scene. The editor process (`illusion::editor_play`,
/// EditorPhysicsQueries) implements it with Jolt and registers with EditorContext::setPhysicsQueries();
/// without one, the command fails with ErrorKind::Unsupported for that mode.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Transform.hpp>

#include <span>

namespace illusion
{
class World;
} // namespace illusion

namespace illusion::editor
{

/// One entity that falls, with the entities that move with it.
struct DropQuery
{
    std::span<const Entity> members; ///< The entity and its descendants; their Colliders fall together.
    Aabb box;                        ///< World-space box around what they draw (or their position).
};

/// The world-space pose of an entity of @p world, also when its Transform is not up to date yet.
using WorldPoseFunction = Transform (*)(World& world, Entity entity);

/// Physics queries on the edit World (see the file documentation). Use it on the editor thread.
class PhysicsQueryHost
{
public:
    virtual ~PhysicsQueryHost() = default;

    /// Writes to @p out (the size of @p queries) how far each query's entities fall straight down (0 or more,
    /// in world units) before they touch the Collider of an entity outside every query, or the ground plane
    /// y = 0. Colliders of the members are swept down; members without any fall from the bottom centre of
    /// DropQuery::box (as do members whose only Colliders are triangle meshes, which cannot be swept). Every
    /// Collider counts as a static surface, with or without a RigidBody; sensors and
    /// Inactive entities do not. Entities that start inside a surface do not move. @p pose places every
    /// entity; the host does not change @p world.
    virtual void dropDistances(World& world, WorldPoseFunction pose, std::span<const DropQuery> queries,
                               std::span<f32> out) = 0;
};

} // namespace illusion::editor
