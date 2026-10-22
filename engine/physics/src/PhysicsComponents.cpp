#include <illusion/physics/PhysicsComponents.hpp>

namespace illusion
{
namespace
{

constexpr std::string_view kRigidBodyRequires[] = {"illusion.Transform", "illusion.Collider"};
constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};
constexpr f64 kMaxLayer = static_cast<f64>(kMaxPhysicsLayers - 1);

} // namespace

void reflectPhysicsComponents(ComponentRegistry& registry)
{
    reflect<RigidBody>("illusion.RigidBody", 1, registry)
        .meta({.displayName = "Rigid body",
               .category = "Physics",
               .icon = "weight",
               .description = "Physical properties of a body simulated by the PhysicsWorld.",
               .docsAnchor = "manual/physics.md#bodies",
               .requiredComponents = kRigidBodyRequires})
        .field("motionType", &RigidBody::motionType)
        .field("mass", &RigidBody::mass,
               {.description = "Mass; 0 computes it from the collider volume (1000 kg/m³).",
                .unit = Unit::Kilograms,
                .min = 0.0})
        .field("friction", &RigidBody::friction, {.min = 0.0, .softMax = 1.0})
        .field("restitution", &RigidBody::restitution, {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("linearDamping", &RigidBody::linearDamping, {.min = 0.0, .advanced = true})
        .field("angularDamping", &RigidBody::angularDamping, {.min = 0.0, .advanced = true})
        .field("gravityFactor", &RigidBody::gravityFactor)
        .field("layer", &RigidBody::layer, {.min = 0.0, .max = kMaxLayer, .hint = FieldHint::Layer})
        .field("sensor", &RigidBody::sensor)
        .field("allowSleeping", &RigidBody::allowSleeping, {.advanced = true})
        .field("continuousCollision", &RigidBody::continuousCollision, {.advanced = true});
    reflect<Collider>("illusion.Collider", 1, registry)
        .meta({.displayName = "Collider",
               .category = "Physics",
               .icon = "select_all",
               .description = "Collision shape of a body, relative to the Entity's Transform.",
               .docsAnchor = "manual/physics.md#colliders",
               .requiredComponents = kRequiresTransform})
        .field("shape", &Collider::shape)
        .field("halfExtents", &Collider::halfExtents, {.unit = Unit::Meters, .min = 0.0})
        .field("radius", &Collider::radius, {.unit = Unit::Meters, .min = 0.0})
        .field("halfHeight", &Collider::halfHeight, {.unit = Unit::Meters, .min = 0.0})
        .field("offset", &Collider::offset, {.unit = Unit::Meters})
        .field("rotation", &Collider::rotation)
        .field("mesh", &Collider::mesh,
               {.description = "ConvexHull, Mesh: the mesh asset the shape is built from.",
                .assetKind = AssetKind::Mesh})
        .field("meshIndex", &Collider::meshIndex,
               {.description = "Mesh of the model when the asset is a model file.", .advanced = true});
    reflect<CharacterController>("illusion.CharacterController", 1, registry)
        .meta({.displayName = "Character controller",
               .category = "Physics",
               .icon = "directions_walk",
               .description = "An upright capsule moved by game code that walks up steps and slopes.",
               .docsAnchor = "manual/physics.md#characters",
               .requiredComponents = kRequiresTransform})
        .field("radius", &CharacterController::radius, {.unit = Unit::Meters, .min = 0.0})
        .field("height", &CharacterController::height, {.unit = Unit::Meters, .min = 0.0})
        .field("maxSlope", &CharacterController::maxSlope,
               {.unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 1.5708,
                .hint = FieldHint::Angle})
        .field("stepHeight", &CharacterController::stepHeight, {.unit = Unit::Meters, .min = 0.0})
        .field("mass", &CharacterController::mass, {.unit = Unit::Kilograms, .min = 0.0})
        .field("maxPushForce", &CharacterController::maxPushForce, {.unit = Unit::Newtons, .min = 0.0})
        .field("gravityFactor", &CharacterController::gravityFactor)
        .field("layer", &CharacterController::layer,
               {.min = 0.0, .max = kMaxLayer, .hint = FieldHint::Layer});
    reflect<Joint>("illusion.Joint", 1, registry)
        .meta({.displayName = "Joint",
               .category = "Physics",
               .icon = "link",
               .description = "Connects the body of the Entity to another body or to the world.",
               .docsAnchor = "manual/physics.md#joints",
               .requiredComponents = kRigidBodyRequires})
        .field("type", &Joint::type)
        .field("connected", &Joint::connected)
        .field("anchor", &Joint::anchor, {.unit = Unit::Meters})
        .field("connectedAnchor", &Joint::connectedAnchor, {.unit = Unit::Meters})
        .field("axis", &Joint::axis)
        .field("limited", &Joint::limited)
        .field("minLimit", &Joint::minLimit, {.description = "Hinge: radians; Slider and Distance: m."})
        .field("maxLimit", &Joint::maxLimit, {.description = "Hinge: radians; Slider and Distance: m."})
        .field("motorSpeed", &Joint::motorSpeed, {.description = "Hinge: rad/s; Slider: m/s."})
        .field("motorMaxForce", &Joint::motorMaxForce,
               {.description = "Hinge: N·m; Slider: N. 0 turns the motor off.", .min = 0.0})
        .field("breakForce", &Joint::breakForce,
               {.description = "Force that breaks the joint; 0 never breaks.",
                .unit = Unit::Newtons,
                .min = 0.0});

    reflectRuntime<PhysicsBody>("illusion.PhysicsBody", {.displayName = "Physics body"}, registry);
    reflectRuntime<CharacterBody>("illusion.CharacterBody", {.displayName = "Character body"}, registry);
    reflectRuntime<PhysicsJoint>("illusion.PhysicsJoint", {.displayName = "Physics joint"}, registry);
}

} // namespace illusion
