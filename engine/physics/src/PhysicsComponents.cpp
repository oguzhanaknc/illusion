#include <illusion/physics/PhysicsComponents.hpp>

namespace illusion
{

void reflectPhysicsComponents(ComponentRegistry& registry)
{
    reflect<RigidBody>("illusion.RigidBody", 1, registry)
        .field("motionType", &RigidBody::motionType)
        .field("mass", &RigidBody::mass)
        .field("friction", &RigidBody::friction)
        .field("restitution", &RigidBody::restitution)
        .field("linearDamping", &RigidBody::linearDamping)
        .field("angularDamping", &RigidBody::angularDamping)
        .field("gravityFactor", &RigidBody::gravityFactor)
        .field("layer", &RigidBody::layer)
        .field("sensor", &RigidBody::sensor)
        .field("allowSleeping", &RigidBody::allowSleeping)
        .field("continuousCollision", &RigidBody::continuousCollision);
    reflect<Collider>("illusion.Collider", 1, registry)
        .field("shape", &Collider::shape)
        .field("halfExtents", &Collider::halfExtents)
        .field("radius", &Collider::radius)
        .field("halfHeight", &Collider::halfHeight)
        .field("offset", &Collider::offset)
        .field("rotation", &Collider::rotation);
}

} // namespace illusion
