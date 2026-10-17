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
#include <vector>

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

/// Result of PhysicsWorld::shapeCast().
struct ShapeCastHit
{
    Entity entity;       ///< The Entity that was hit.
    Vec3 point{0.0f};    ///< Contact point on the hit body in world space.
    Vec3 normal{0.0f};   ///< Contact normal, pointing from the hit body towards the cast shape.
    f32 distance = 0.0f; ///< How far the shape moved before touching (0 if it started overlapping).
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
    /// writes the Transforms of dynamic bodies and characters, interpolated between the last two steps:
    /// - Entities with Transform, RigidBody and Collider get a body (and a PhysicsBody component);
    ///   Entities with Transform and CharacterController get a character (and a CharacterBody); Entities
    ///   with a Joint get a joint once its bodies exist (and a PhysicsJoint).
    /// - Bodies, characters and joints of destroyed Entities, or of Entities that lost their components,
    ///   are removed; joints are also removed with their bodies.
    /// - Kinematic and static bodies follow their Transforms; a dynamic body or a character whose
    ///   Transform was changed by game code is teleported there.
    void update(f64 deltaSeconds);

    /// Recreates the body, character and joint of @p entity on the next update(), applying changes to
    /// its RigidBody, Collider, CharacterController and Joint (and the Transform's scale). Joints
    /// attached to a rebuilt body are recreated too.
    void rebuild(Entity entity);

    /// Number of bodies (characters count one each).
    [[nodiscard]] u32 bodyCount() const noexcept;

    /// Number of characters.
    [[nodiscard]] u32 characterCount() const noexcept;

    /// Number of joints, including broken ones.
    [[nodiscard]] u32 jointCount() const noexcept;

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

    /// @name Collision geometry
    /// @{

    /// Creates triangle-mesh geometry for Collider::mesh with ColliderShape::Mesh, from @p vertices and
    /// three indices per triangle (counter-clockwise seen from the front; both sides collide). Mesh
    /// colliders only work on static and kinematic bodies. Returns an invalid handle (and logs a warning)
    /// for empty or out-of-range input.
    PhysicsMeshHandle createMesh(std::span<const Vec3> vertices, std::span<const u32> indices);

    /// Creates the convex hull of @p points for Collider::mesh with ColliderShape::ConvexHull (any motion
    /// type). Returns an invalid handle (and logs a warning) if the points are degenerate, for example
    /// all on one plane.
    PhysicsMeshHandle createConvexHull(std::span<const Vec3> points);

    /// Releases geometry. Bodies already using it keep it until they are rebuilt or removed.
    void destroyMesh(PhysicsMeshHandle mesh);

    /// True if @p mesh is a live handle.
    [[nodiscard]] bool isMeshValid(PhysicsMeshHandle mesh) const noexcept;

    /// @}

    /// @name Characters
    /// These act on the CharacterController of @p entity and do nothing (returning zero) if it has no
    /// character yet.
    /// @{

    /// Sets the velocity in m/s. Gravity is added while the character is in the air, and a downward
    /// velocity stops on the ground; to walk, set the horizontal part every frame, to jump, set an upward
    /// one (see isCharacterGrounded()).
    void setCharacterVelocity(Entity entity, const Vec3& velocity);

    /// Velocity in m/s.
    [[nodiscard]] Vec3 characterVelocity(Entity entity) const;

    /// True if the character stands on walkable ground.
    [[nodiscard]] bool isCharacterGrounded(Entity entity) const;

    /// @}

    /// @name Joints
    /// @{

    /// Current hinge angle in radians, slider position in m or distance between the anchors in m, relative
    /// to the pose when the joint was created (Distance: absolute). 0 for other joints and Entities
    /// without a joint.
    [[nodiscard]] f32 jointPosition(Entity entity) const;

    /// Entities whose Joint broke during the last update() (see Joint::breakForce), sorted.
    [[nodiscard]] std::span<const Entity> brokenJoints() const noexcept;

    /// @}

    /// Casts a ray from @p origin along @p direction (need not be normalized) up to @p maxDistance and
    /// returns the closest hit on a body whose layer is in @p layerMask (bit i = layer i). Sensors and
    /// the body of @p ignore are skipped.
    [[nodiscard]] std::optional<RaycastHit> raycast(const Vec3& origin, const Vec3& direction,
                                                    f32 maxDistance, u32 layerMask = 0xFFFFFFFFu,
                                                    Entity ignore = kNullEntity) const;

    /// Moves @p shape (any shape except Mesh; Collider::offset and rotation apply) from @p position with
    /// @p rotation along @p direction (need not be normalized) up to @p maxDistance and returns the first
    /// body it touches whose layer is in @p layerMask. Sensors and the body of @p ignore are skipped.
    [[nodiscard]] std::optional<ShapeCastHit> shapeCast(const Collider& shape, const Vec3& position,
                                                        const Quat& rotation, const Vec3& direction,
                                                        f32 maxDistance, u32 layerMask = 0xFFFFFFFFu,
                                                        Entity ignore = kNullEntity) const;

    /// Replaces @p out with the Entities whose bodies overlap @p shape (any shape except Mesh) placed at
    /// @p position with @p rotation, sorted, each once. Sensors, bodies outside @p layerMask and the body
    /// of @p ignore are skipped. Returns the count. Reuse @p out to avoid allocations.
    u32 overlap(const Collider& shape, const Vec3& position, const Quat& rotation, std::vector<Entity>& out,
                u32 layerMask = 0xFFFFFFFFu, Entity ignore = kNullEntity) const;

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
