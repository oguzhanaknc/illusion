#pragma once

/// @file
/// The spring arm's collision over a PhysicsWorld (docs/adr/0064-virtual-cameras.md).

#include <illusion/camera/CameraWorld.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>

#include <optional>

namespace illusion
{

class PhysicsWorld;
class World;

/// CameraWorldCollision through PhysicsWorld::shapeCast() with a sphere. The casts pass through the bodies
/// of the ignored target's carrier (the target or its nearest ancestor with a CharacterController or
/// RigidBody) and of the carrier's ancestors and descendants. Sensors are never hit.
class PhysicsCameraCollision final : public CameraWorldCollision
{
public:
    /// @p world and @p physics must outlive this object.
    PhysicsCameraCollision(const World& world, const PhysicsWorld& physics) noexcept;

    /// Sets the target whose bodies the casts skip (null: none).
    void setIgnored(Entity target) noexcept override;

    /// See CameraCollisionQuery::sphereCast().
    [[nodiscard]] std::optional<f32> sphereCast(Vec3 origin, Vec3 direction, f32 maxDistance, f32 radius,
                                                u32 layerMask) const noexcept override;

private:
    const World* m_world;
    const PhysicsWorld* m_physics;
    Entity m_carrier;
};

} // namespace illusion
