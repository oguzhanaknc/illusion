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
    reflect<CharacterController>("illusion.CharacterController", 1, registry)
        .field("radius", &CharacterController::radius)
        .field("height", &CharacterController::height)
        .field("maxSlope", &CharacterController::maxSlope)
        .field("stepHeight", &CharacterController::stepHeight)
        .field("mass", &CharacterController::mass)
        .field("maxPushForce", &CharacterController::maxPushForce)
        .field("gravityFactor", &CharacterController::gravityFactor)
        .field("layer", &CharacterController::layer);
    reflect<Joint>("illusion.Joint", 1, registry)
        .field("type", &Joint::type)
        .field("connected", &Joint::connected)
        .field("anchor", &Joint::anchor)
        .field("connectedAnchor", &Joint::connectedAnchor)
        .field("axis", &Joint::axis)
        .field("limited", &Joint::limited)
        .field("minLimit", &Joint::minLimit)
        .field("maxLimit", &Joint::maxLimit)
        .field("motorSpeed", &Joint::motorSpeed)
        .field("motorMaxForce", &Joint::motorMaxForce)
        .field("breakForce", &Joint::breakForce);
}

} // namespace illusion
