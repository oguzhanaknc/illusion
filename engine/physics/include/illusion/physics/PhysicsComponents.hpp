#pragma once

/// @file
/// ECS components simulated by PhysicsWorld. An Entity with Transform, RigidBody and Collider gets a
/// physics body on the next PhysicsWorld::update().

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>

namespace illusion
{

/// How a body moves.
enum class MotionType : u8
{
    Static,    ///< Never moves (level geometry). Follows its Transform if the Transform is changed.
    Kinematic, ///< Moved by its Transform; pushes dynamic bodies but is not affected by them.
    Dynamic,   ///< Simulated: gravity, forces and collisions move it and update its Transform.
};

/// Shape of a Collider.
enum class ColliderShape : u8
{
    Box,        ///< Box with half extents Collider::halfExtents.
    Sphere,     ///< Sphere with Collider::radius.
    Capsule,    ///< Capsule along the local Y axis: Collider::halfHeight (cylinder part) and radius.
    Cylinder,   ///< Cylinder along the local Y axis: Collider::halfHeight and radius.
    ConvexHull, ///< Convex hull of Collider::mesh, or Collider::geometry from
                ///< PhysicsWorld::createConvexHull().
    Mesh, ///< Triangle mesh of Collider::mesh, or Collider::geometry from PhysicsWorld::createMesh(); static
          ///< and kinematic bodies only.
};

/// Handle to collision geometry created by PhysicsWorld::createMesh() or PhysicsWorld::createConvexHull().
struct PhysicsMeshHandle
{
    u32 index = 0;      ///< Slot in the PhysicsWorld's geometry table.
    u32 generation = 0; ///< 0 for the invalid handle; changes when the slot is reused.

    /// True unless this is the default (invalid) handle.
    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }

    /// Handles are equal when index and generation match.
    friend constexpr bool operator==(const PhysicsMeshHandle&, const PhysicsMeshHandle&) noexcept = default;
};

/// Physical properties of a body. Changes take effect when the body is created; call
/// PhysicsWorld::rebuild() to apply them to an existing body.
struct RigidBody
{
    MotionType motionType = MotionType::Dynamic; ///< How the body moves.
    f32 mass = 0.0f;            ///< Mass in kg; 0 computes it from the collider volume (1000 kg/m³).
    f32 friction = 0.5f;        ///< Friction coefficient, usually in [0, 1].
    f32 restitution = 0.0f;     ///< Bounciness in [0, 1].
    f32 linearDamping = 0.05f;  ///< Linear velocity damping per second.
    f32 angularDamping = 0.05f; ///< Angular velocity damping per second.
    f32 gravityFactor = 1.0f;   ///< Multiplier for gravity (0 disables gravity for this body).
    u32 layer = 0;       ///< Collision layer in [0, kMaxPhysicsLayers); see PhysicsWorld::setLayersCollide().
    bool sensor = false; ///< Reports contacts (as a trigger) but does not collide.
    bool allowSleeping = true;        ///< Lets the body sleep when it comes to rest.
    bool continuousCollision = false; ///< Prevents fast bodies from passing through thin geometry.
};

/// Collision shape of a body, relative to the Entity's Transform. The Transform's scale scales the
/// shape (spheres and capsules use a uniform scale).
struct Collider
{
    ColliderShape shape = ColliderShape::Box; ///< Shape type.
    Vec3 halfExtents{0.5f};                   ///< Box: half size along each axis.
    f32 radius = 0.5f;                        ///< Sphere, Capsule, Cylinder: radius.
    f32 halfHeight = 0.5f;                    ///< Capsule, Cylinder: half the height of the straight part.
    Vec3 offset{0.0f};                        ///< Shape position relative to the Entity.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};    ///< Shape rotation relative to the Entity.
    /// ConvexHull, Mesh: the mesh asset to build the shape from (`model.glb#mesh/N`, `#node/<name>` or
    /// `builtin:/mesh/...`), cooked by a PhysicsAssetResolver. Leave it empty to set geometry in code.
    AssetRef mesh{};
    u32 meshIndex = 0; ///< Mesh of the model when @ref mesh names a model file without a fragment.
    /// ConvexHull, Mesh: the geometry, from PhysicsWorld::createMesh() / createConvexHull() or written by a
    /// PhysicsAssetResolver for @ref mesh. Runtime data, not saved in scenes.
    PhysicsMeshHandle geometry;
    u64 geometryKey = 0; ///< Runtime: what a PhysicsAssetResolver cooked geometry from (0 for code).
};

/// Runtime link between an Entity and its physics body. Added and removed by PhysicsWorld; not saved in
/// scenes.
struct PhysicsBody
{
    static constexpr u32 kInvalidId = 0xFFFFFFFFu; ///< No body.

    u32 id = kInvalidId;                           ///< Body id inside the PhysicsWorld.
    Vec3 previousPosition{0.0f};                   ///< Pose before the last step (for interpolation).
    Quat previousRotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Rotation before the last step.
    Vec3 writtenPosition{0.0f};                    ///< Position last written to the Transform.
    Quat writtenRotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< Rotation last written to the Transform.
};

/// A character moved by game code instead of by forces: an upright capsule that walks up steps and slopes,
/// slides along walls, rides moving platforms and pushes dynamic bodies. The Transform position is at the
/// character's feet; its rotation is left to game code. Control it with PhysicsWorld::setCharacterVelocity().
/// Changes take effect when the character is created; call PhysicsWorld::rebuild() to apply them later.
struct CharacterController
{
    f32 radius = 0.3f;          ///< Capsule radius in m.
    f32 height = 1.8f;          ///< Total height in m (at least 2 × radius).
    f32 maxSlope = 0.87266463f; ///< Steepest walkable slope in radians (50°); steeper slopes are slid down.
    f32 stepHeight = 0.35f;     ///< Highest step in m the character walks up (and snaps down to).
    f32 mass = 70.0f;           ///< Mass in kg; pushes down on what the character stands on.
    f32 maxPushForce = 100.0f;  ///< Strongest force in N the character pushes dynamic bodies with.
    f32 gravityFactor = 1.0f;   ///< Multiplier for gravity while the character is in the air.
    u32 layer = 0;              ///< Collision layer in [0, kMaxPhysicsLayers).
};

/// Runtime state of a CharacterController, written by PhysicsWorld::update(). Not saved in scenes.
struct CharacterBody
{
    static constexpr u32 kInvalidId = 0xFFFFFFFFu; ///< No character.

    u32 id = kInvalidId;                 ///< Character id inside the PhysicsWorld.
    Vec3 velocity{0.0f};                 ///< Velocity in m/s after the last update.
    bool grounded = false;               ///< Standing on walkable ground.
    Vec3 groundNormal{0.0f, 1.0f, 0.0f}; ///< Normal of the ground (up when in the air).
    Entity ground;                       ///< Entity stood on (null in the air).
    Vec3 previousPosition{0.0f};         ///< Position before the last step (for interpolation).
    Vec3 writtenPosition{0.0f};          ///< Position last written to the Transform.
};

/// Kind of Joint.
enum class JointType : u8
{
    Fixed,    ///< Glues the bodies together.
    Point,    ///< Ball and socket: the anchor points stay together, rotation is free.
    Hinge,    ///< Rotation around Joint::axis only (doors, wheels); optional limits and motor.
    Slider,   ///< Movement along Joint::axis only (pistons, drawers); optional limits and motor.
    Distance, ///< Keeps the anchors between Joint::minLimit and Joint::maxLimit apart (ropes, rods); both
              ///< 0 keeps their initial distance.
};

/// Connects the body of its Entity to the body of Joint::connected (or to the world). The Entity and the
/// connected Entity need a body (RigidBody + Collider), and at least one of them must be dynamic. The joint
/// is created in the bodies' poses at that moment. Changes take effect when the joint is created (except
/// motorSpeed, which is applied every update); call PhysicsWorld::rebuild() on the Entity to apply them.
struct Joint
{
    JointType type = JointType::Point; ///< Kind of joint.
    Entity connected;                  ///< The other body; null connects to the world.
    Vec3 anchor{0.0f};                 ///< Joint point in this Entity's local space.
    Vec3 connectedAnchor{0.0f};        ///< Distance: the other point, in the connected Entity's local
                                       ///< space (world space without one).
    Vec3 axis{1.0f, 0.0f, 0.0f};       ///< Hinge and Slider: axis in this Entity's local space.
    bool limited = false;              ///< Hinge and Slider: enforce minLimit and maxLimit.
    f32 minLimit = 0.0f;               ///< Hinge: radians; Slider and Distance: m.
    f32 maxLimit = 0.0f;               ///< Hinge: radians; Slider and Distance: m.
    f32 motorSpeed = 0.0f;             ///< Hinge: rad/s; Slider: m/s. Used when motorMaxForce > 0.
    f32 motorMaxForce = 0.0f;          ///< Hinge: N·m; Slider: N. 0 turns the motor off.
    f32 breakForce = 0.0f;             ///< Force in N that breaks the joint; 0 never breaks.
};

/// Runtime state of a Joint, added by PhysicsWorld. Not saved in scenes.
struct PhysicsJoint
{
    static constexpr u32 kInvalidId = 0xFFFFFFFFu; ///< No joint.

    u32 id = kInvalidId; ///< Joint id inside the PhysicsWorld (kInvalidId if it could not be created).
    bool broken = false; ///< The joint broke (see Joint::breakForce).
};

/// Number of collision layers.
inline constexpr u32 kMaxPhysicsLayers = 16;

/// Reflects RigidBody ("illusion.RigidBody"), Collider ("illusion.Collider"), CharacterController
/// ("illusion.CharacterController") and Joint ("illusion.Joint") with editor metadata, and the runtime state
/// PhysicsBody, CharacterBody and PhysicsJoint ("illusion.<Name>", TypeMeta::transient).
void reflectPhysicsComponents(ComponentRegistry& registry = ComponentRegistry::instance());

} // namespace illusion

/// Names of MotionType values in scene files.
template <>
struct illusion::EnumReflection<illusion::MotionType>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Static", 0}, illusion::EnumValue{"Kinematic", 1},
                                        illusion::EnumValue{"Dynamic", 2}};
};

/// Names of ColliderShape values in scene files.
template <>
struct illusion::EnumReflection<illusion::ColliderShape>
{
    /// Value names.
    static constexpr std::array kValues{
        illusion::EnumValue{"Box", 0},        illusion::EnumValue{"Sphere", 1},
        illusion::EnumValue{"Capsule", 2},    illusion::EnumValue{"Cylinder", 3},
        illusion::EnumValue{"ConvexHull", 4}, illusion::EnumValue{"Mesh", 5}};
};

/// Names of JointType values in scene files.
template <>
struct illusion::EnumReflection<illusion::JointType>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Fixed", 0}, illusion::EnumValue{"Point", 1},
                                        illusion::EnumValue{"Hinge", 2}, illusion::EnumValue{"Slider", 3},
                                        illusion::EnumValue{"Distance", 4}};
};
