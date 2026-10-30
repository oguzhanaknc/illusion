#include <illusion/physics/PhysicsWorldService.hpp>

#include <algorithm>

namespace illusion
{

// Out of line so that the vtable is emitted in the library, which only the host links.
PhysicsWorldService::~PhysicsWorldService() = default;

u32 PhysicsWorldService::overlap(const Collider& shape, const Vec3& position, const Quat& rotation,
                                 std::span<Entity> out, u32 layerMask, Entity ignore) const
{
    // The vector keeps its capacity: overlap() allocates only when a query finds more bodies than ever.
    m_overlaps.clear();
    const u32 count = m_physics.overlap(shape, position, rotation, m_overlaps, layerMask, ignore);
    std::copy_n(m_overlaps.begin(), std::min(out.size(), m_overlaps.size()), out.begin());
    return count;
}

void PhysicsWorldService::setGravity(const Vec3& gravity)
{
    m_physics.setGravity(gravity);
}

Vec3 PhysicsWorldService::gravity() const
{
    return m_physics.gravity();
}

void PhysicsWorldService::setLayersCollide(u32 a, u32 b, bool collide)
{
    m_physics.setLayersCollide(a, b, collide);
}

bool PhysicsWorldService::layersCollide(u32 a, u32 b) const
{
    return m_physics.layersCollide(a, b);
}

void PhysicsWorldService::rebuild(Entity entity)
{
    m_physics.rebuild(entity);
}

void PhysicsWorldService::setLinearVelocity(Entity entity, const Vec3& velocity)
{
    m_physics.setLinearVelocity(entity, velocity);
}

Vec3 PhysicsWorldService::linearVelocity(Entity entity) const
{
    return m_physics.linearVelocity(entity);
}

void PhysicsWorldService::setAngularVelocity(Entity entity, const Vec3& velocity)
{
    m_physics.setAngularVelocity(entity, velocity);
}

Vec3 PhysicsWorldService::angularVelocity(Entity entity) const
{
    return m_physics.angularVelocity(entity);
}

void PhysicsWorldService::addForce(Entity entity, const Vec3& force)
{
    m_physics.addForce(entity, force);
}

void PhysicsWorldService::addTorque(Entity entity, const Vec3& torque)
{
    m_physics.addTorque(entity, torque);
}

void PhysicsWorldService::addImpulse(Entity entity, const Vec3& impulse)
{
    m_physics.addImpulse(entity, impulse);
}

f32 PhysicsWorldService::mass(Entity entity) const
{
    return m_physics.mass(entity);
}

bool PhysicsWorldService::isAwake(Entity entity) const
{
    return m_physics.isAwake(entity);
}

PhysicsMeshHandle PhysicsWorldService::createMesh(std::span<const Vec3> vertices,
                                                  std::span<const u32> indices)
{
    return m_physics.createMesh(vertices, indices);
}

PhysicsMeshHandle PhysicsWorldService::createConvexHull(std::span<const Vec3> points)
{
    return m_physics.createConvexHull(points);
}

void PhysicsWorldService::destroyMesh(PhysicsMeshHandle mesh)
{
    m_physics.destroyMesh(mesh);
}

bool PhysicsWorldService::isMeshValid(PhysicsMeshHandle mesh) const
{
    return m_physics.isMeshValid(mesh);
}

void PhysicsWorldService::setCharacterVelocity(Entity entity, const Vec3& velocity)
{
    m_physics.setCharacterVelocity(entity, velocity);
}

Vec3 PhysicsWorldService::characterVelocity(Entity entity) const
{
    return m_physics.characterVelocity(entity);
}

bool PhysicsWorldService::isCharacterGrounded(Entity entity) const
{
    return m_physics.isCharacterGrounded(entity);
}

f32 PhysicsWorldService::jointPosition(Entity entity) const
{
    return m_physics.jointPosition(entity);
}

std::span<const Entity> PhysicsWorldService::brokenJoints() const
{
    return m_physics.brokenJoints();
}

std::optional<RaycastHit> PhysicsWorldService::raycast(const Vec3& origin, const Vec3& direction,
                                                       f32 maxDistance, u32 layerMask, Entity ignore) const
{
    return m_physics.raycast(origin, direction, maxDistance, layerMask, ignore);
}

std::optional<ShapeCastHit> PhysicsWorldService::shapeCast(const Collider& shape, const Vec3& position,
                                                           const Quat& rotation, const Vec3& direction,
                                                           f32 maxDistance, u32 layerMask,
                                                           Entity ignore) const
{
    return m_physics.shapeCast(shape, position, rotation, direction, maxDistance, layerMask, ignore);
}

std::span<const ContactEvent> PhysicsWorldService::contactEvents() const
{
    return m_physics.contactEvents();
}

} // namespace illusion
