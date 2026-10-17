#pragma once

/// @file
/// ECS components simulated by PhysicsWorld. An Entity with Transform, RigidBody and Collider gets a
/// physics body on the next PhysicsWorld::update().

#include <illusion/core/Math.hpp>
#include <illusion/ecs/Component.hpp>
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
    Box,      ///< Box with half extents Collider::halfExtents.
    Sphere,   ///< Sphere with Collider::radius.
    Capsule,  ///< Capsule along the local Y axis: Collider::halfHeight (cylinder part) and radius.
    Cylinder, ///< Cylinder along the local Y axis: Collider::halfHeight and radius.
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

/// Number of collision layers.
inline constexpr u32 kMaxPhysicsLayers = 16;

/// Reflects RigidBody ("illusion.RigidBody") and Collider ("illusion.Collider").
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
    static constexpr std::array kValues{illusion::EnumValue{"Box", 0}, illusion::EnumValue{"Sphere", 1},
                                        illusion::EnumValue{"Capsule", 2},
                                        illusion::EnumValue{"Cylinder", 3}};
};
