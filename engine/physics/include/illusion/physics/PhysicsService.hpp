#pragma once

/// @file
/// The physics part of the scripting API: what a game module may ask of the host's PhysicsWorld.
///
/// @code
/// if (illusion::PhysicsService* physics = context.service<illusion::PhysicsService>())
/// {
///     physics->addImpulse(player, illusion::Vec3(0.0f, 5.0f, 0.0f));
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsTypes.hpp>

#include <optional>
#include <span>

namespace illusion
{

/// Rigid-body physics of one Simulation World, implemented by the host (PhysicsWorldService over a
/// PhysicsWorld) and found by modules with `context.service<PhysicsService>()`. Every call runs host code
/// on host objects, so geometry created here stays valid across hot reloads of the module (ADR 0041).
/// The functions behave like the PhysicsWorld functions of the same name. Adding a function changes the
/// module API version.
/// @ingroup scripting_api
class PhysicsService : NonCopyable
{
public:
    /// Sets the gravity in m/s².
    virtual void setGravity(const Vec3& gravity) = 0;
    /// Gravity in m/s².
    [[nodiscard]] virtual Vec3 gravity() const = 0;
    /// Sets whether bodies on layers @p a and @p b collide.
    virtual void setLayersCollide(u32 a, u32 b, bool collide) = 0;
    /// True if bodies on layers @p a and @p b collide.
    [[nodiscard]] virtual bool layersCollide(u32 a, u32 b) const = 0;
    /// Recreates the body of @p entity from its RigidBody and Collider.
    virtual void rebuild(Entity entity) = 0;

    /// Sets the linear velocity of a dynamic body in m/s.
    virtual void setLinearVelocity(Entity entity, const Vec3& velocity) = 0;
    /// Linear velocity of a body in m/s (zero without a body).
    [[nodiscard]] virtual Vec3 linearVelocity(Entity entity) const = 0;
    /// Sets the angular velocity of a dynamic body in rad/s.
    virtual void setAngularVelocity(Entity entity, const Vec3& velocity) = 0;
    /// Angular velocity of a body in rad/s (zero without a body).
    [[nodiscard]] virtual Vec3 angularVelocity(Entity entity) const = 0;
    /// Adds a force in N for the next step.
    virtual void addForce(Entity entity, const Vec3& force) = 0;
    /// Adds a torque in N·m for the next step.
    virtual void addTorque(Entity entity, const Vec3& torque) = 0;
    /// Changes the velocity at once by an impulse in N·s.
    virtual void addImpulse(Entity entity, const Vec3& impulse) = 0;
    /// Mass of a body in kg (0 without a dynamic body).
    [[nodiscard]] virtual f32 mass(Entity entity) const = 0;
    /// True if the body is simulated (not sleeping).
    [[nodiscard]] virtual bool isAwake(Entity entity) const = 0;

    /// Creates triangle mesh geometry for Collider::geometry; the host owns it.
    virtual PhysicsMeshHandle createMesh(std::span<const Vec3> vertices, std::span<const u32> indices) = 0;
    /// Creates convex hull geometry for Collider::geometry; the host owns it.
    virtual PhysicsMeshHandle createConvexHull(std::span<const Vec3> points) = 0;
    /// Destroys geometry once no Collider uses it.
    virtual void destroyMesh(PhysicsMeshHandle mesh) = 0;
    /// True if @p mesh names live geometry.
    [[nodiscard]] virtual bool isMeshValid(PhysicsMeshHandle mesh) const = 0;

    /// Sets the velocity a CharacterController moves with in m/s.
    virtual void setCharacterVelocity(Entity entity, const Vec3& velocity) = 0;
    /// Velocity of a CharacterController in m/s.
    [[nodiscard]] virtual Vec3 characterVelocity(Entity entity) const = 0;
    /// True if a CharacterController stands on the ground.
    [[nodiscard]] virtual bool isCharacterGrounded(Entity entity) const = 0;
    /// Position of a hinge (radians) or slider (metres) Joint.
    [[nodiscard]] virtual f32 jointPosition(Entity entity) const = 0;
    /// Joints that broke during the last update; valid until the next one.
    [[nodiscard]] virtual std::span<const Entity> brokenJoints() const = 0;

    /// Closest body hit by a ray from @p origin along the normalized @p direction.
    [[nodiscard]] virtual std::optional<RaycastHit> raycast(const Vec3& origin, const Vec3& direction,
                                                            f32 maxDistance, u32 layerMask,
                                                            Entity ignore) const = 0;
    /// First body hit by @p shape moved from @p position along the normalized @p direction.
    [[nodiscard]] virtual std::optional<ShapeCastHit> shapeCast(const Collider& shape, const Vec3& position,
                                                                const Quat& rotation, const Vec3& direction,
                                                                f32 maxDistance, u32 layerMask,
                                                                Entity ignore) const = 0;
    /// Writes the Entities whose bodies overlap @p shape into @p out (as many as fit) and returns how many
    /// there are.
    [[nodiscard]] virtual u32 overlap(const Collider& shape, const Vec3& position, const Quat& rotation,
                                      std::span<Entity> out, u32 layerMask, Entity ignore) const = 0;
    /// Contact events of the last update; valid until the next one.
    [[nodiscard]] virtual std::span<const ContactEvent> contactEvents() const = 0;

protected:
    PhysicsService() = default;
    virtual ~PhysicsService() = default;
};

} // namespace illusion
