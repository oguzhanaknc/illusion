#pragma once

/// @file
/// Plain results of physics queries and events, shared by PhysicsWorld and PhysicsService.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

namespace illusion
{

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
    /// Begin: speed in m/s at which the bodies approached each other along the normal when they touched
    /// (0 if they were moving apart), for impact sounds and effects.
    f32 speed = 0.0f;
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

} // namespace illusion
