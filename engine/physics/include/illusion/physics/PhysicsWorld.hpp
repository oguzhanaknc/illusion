#pragma once

/// @file
/// Rigid-body physics for a World, simulated with Jolt Physics at a fixed time step.
///
/// @code
/// illusion::PhysicsWorld physics(world, jobs);
/// world.create(illusion::Transform{}, illusion::RigidBody{}, illusion::Collider{});
/// // every frame:
/// physics.update(frameSeconds); // steps the simulation and writes Transforms of dynamic bodies
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <memory>
#include <optional>
#include <span>

namespace illusion
{

class JobSystem;
class World;

/// Creation parameters for PhysicsWorld.
struct PhysicsWorldDesc
{
    Vec3 gravity{0.0f, -9.81f, 0.0f};  ///< Gravity in m/s².
    f64 stepSeconds = 1.0 / 60.0;      ///< Fixed simulation step.
    u32 maxStepsPerUpdate = 4;         ///< Steps per update() at most; longer frames slow the simulation.
    u32 maxBodies = 65536;             ///< Maximum number of bodies.
    u32 maxBodyPairs = 65536;          ///< Maximum number of body pairs in the broad phase.
    u32 maxContactConstraints = 16384; ///< Maximum number of contact constraints per step.
    u32 maxContactEvents = 4096;       ///< Contact events kept per update(); more are counted and dropped.
    usize tempMemoryBytes = usize{16} << 20; ///< Scratch memory for one step.
};

/// Kind of contact event.
enum class ContactEventType : u8
{
    Begin, ///< Two bodies started touching.
    End,   ///< Two bodies stopped touching.
};

/// A change in contact between two bodies, reported by PhysicsWorld::contactEvents().
struct ContactEvent
{
    ContactEventType type = ContactEventType::Begin; ///< Began or ended.
    Entity a;                                        ///< First Entity (a.toBits() < b.toBits()).
    Entity b;                                        ///< Second Entity.
    bool sensor = false;                             ///< True if one of the bodies is a sensor.
    Vec3 point{0.0f};                                ///< Begin: a contact point in world space.
    Vec3 normal{0.0f};                               ///< Begin: contact normal, pointing from a to b.
};

/// Result of PhysicsWorld::raycast().
struct RaycastHit
{
    Entity entity;       ///< The Entity that was hit.
    Vec3 point{0.0f};    ///< Hit point in world space.
    Vec3 normal{0.0f};   ///< Surface normal at the hit point.
    f32 distance = 0.0f; ///< Distance from the ray origin.
};

/// Simulates the RigidBody + Collider Entities of a World. The World must outlive the PhysicsWorld.
/// Not thread-safe: call it from one thread, outside of World queries. Simulation work runs on the
/// JobSystem.
class PhysicsWorld : NonCopyable
{
public:
    /// Creates an empty simulation for @p world. @p jobs runs the simulation work.
    PhysicsWorld(World& world, JobSystem& jobs, const PhysicsWorldDesc& desc = {});

    /// Removes every body and the PhysicsBody components.
    ~PhysicsWorld();

    /// Synchronizes bodies with the World, advances the simulation by @p deltaSeconds in fixed steps and
    /// writes the Transforms of dynamic bodies, interpolated between the last two steps:
    /// - Entities with Transform, RigidBody and Collider get a body (and a PhysicsBody component).
    /// - Bodies of destroyed Entities, or of Entities that lost RigidBody or Collider, are removed.
    /// - Kinematic and static bodies follow their Transforms; a dynamic body whose Transform was
    ///   changed by game code is teleported there.
    void update(f64 deltaSeconds);

    /// Recreates the body of @p entity on the next update(), applying changes to its RigidBody and
    /// Collider (and the Transform's scale).
    void rebuild(Entity entity);

    /// Number of bodies.
    [[nodiscard]] u32 bodyCount() const noexcept;

    /// Number of fixed steps simulated so far.
    [[nodiscard]] u64 stepCount() const noexcept;

    /// Sets gravity in m/s².
    void setGravity(const Vec3& gravity);

    /// Current gravity.
    [[nodiscard]] Vec3 gravity() const;

    /// Enables or disables collisions between bodies on layers @p a and @p b (all pairs collide by
    /// default). Takes effect immediately.
    void setLayersCollide(u32 a, u32 b, bool collide);

    /// True if bodies on layers @p a and @p b collide.
    [[nodiscard]] bool layersCollide(u32 a, u32 b) const;

    /// @name Body control
    /// These act on the body of @p entity and do nothing (returning zero) if it has no body yet.
    /// @{

    /// Sets the linear velocity in m/s.
    void setLinearVelocity(Entity entity, const Vec3& velocity);

    /// Linear velocity in m/s.
    [[nodiscard]] Vec3 linearVelocity(Entity entity) const;

    /// Sets the angular velocity in rad/s.
    void setAngularVelocity(Entity entity, const Vec3& velocity);

    /// Angular velocity in rad/s.
    [[nodiscard]] Vec3 angularVelocity(Entity entity) const;

    /// Applies a force in N at the centre of mass during the next step.
    void addForce(Entity entity, const Vec3& force);

    /// Applies a torque in N·m during the next step.
    void addTorque(Entity entity, const Vec3& torque);

    /// Changes the velocity immediately by @p impulse / mass (impulse in N·s).
    void addImpulse(Entity entity, const Vec3& impulse);

    /// Mass in kg (0 for static and kinematic bodies).
    [[nodiscard]] f32 mass(Entity entity) const;

    /// True if the body is simulated (dynamic or kinematic bodies that are not sleeping).
    [[nodiscard]] bool isAwake(Entity entity) const;

    /// @}

    /// Casts a ray from @p origin along @p direction (need not be normalized) up to @p maxDistance and
    /// returns the closest hit on a body whose layer is in @p layerMask (bit i = layer i). Sensors are
    /// ignored.
    [[nodiscard]] std::optional<RaycastHit> raycast(const Vec3& origin, const Vec3& direction,
                                                    f32 maxDistance, u32 layerMask = 0xFFFFFFFFu) const;

    /// Contact events of the last update(), sorted by type and entities.
    [[nodiscard]] std::span<const ContactEvent> contactEvents() const noexcept;

    /// Contact events dropped in the last update() because PhysicsWorldDesc::maxContactEvents was
    /// reached.
    [[nodiscard]] u32 droppedContactEvents() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
