#include "CharacterBodies.hpp"

#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/runtime/PhysicsCameraCollision.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{
namespace
{

// Bodies of the target passed through by one cast before it gives up.
constexpr int kMaxSkips = 8;
// A cast that meets the target goes on this far past the touch, at least, in metres.
constexpr f32 kMinSkip = 0.05f;

} // namespace

PhysicsCameraCollision::PhysicsCameraCollision(const World& world, const PhysicsWorld& physics) noexcept
    : m_world(&world)
    , m_physics(&physics)
{
}

void PhysicsCameraCollision::setIgnored(Entity target) noexcept
{
    m_carrier =
        target.isNull() || !m_world->isAlive(target) ? kNullEntity : detail::carrierOf(*m_world, target);
}

std::optional<f32> PhysicsCameraCollision::sphereCast(Vec3 origin, Vec3 direction, f32 maxDistance,
                                                      f32 radius, u32 layerMask) const noexcept
{
    if (!(maxDistance > 0.0f) || !std::isfinite(maxDistance))
    {
        return std::nullopt;
    }
    Collider sphere;
    sphere.shape = ColliderShape::Sphere;
    sphere.radius = std::max(radius, 1e-3f);
    const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    f32 travelled = 0.0f;
    for (int i = 0; i < kMaxSkips && travelled < maxDistance; ++i)
    {
        const std::optional<ShapeCastHit> hit =
            m_physics->shapeCast(sphere, origin + direction * travelled, identity, direction,
                                 maxDistance - travelled, layerMask, m_carrier);
        if (!hit)
        {
            return std::nullopt;
        }
        if (m_carrier.isNull() || !detail::partOf(*m_world, hit->entity, m_carrier))
        {
            return travelled + hit->distance;
        }
        travelled += hit->distance + std::max(2.0f * sphere.radius, kMinSkip);
    }
    return std::nullopt;
}

} // namespace illusion
