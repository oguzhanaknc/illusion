#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>

namespace illusion
{

namespace
{

constexpr std::string_view kRequires[] = {"illusion.Transform", "illusion.MeshRenderer"};
constexpr std::string_view kRequiresAnimator[] = {"illusion.Animator"};
constexpr std::string_view kRequiresParent[] = {"illusion.Transform", "illusion.Hierarchy",
                                                "illusion.LocalTransform"};

} // namespace

void registerAnimationComponents(ComponentRegistry& registry)
{
    reflect<Animator>("illusion.Animator", 1, registry)
        .meta({.displayName = "Animator",
               .category = "Animation",
               .icon = "directions_run",
               .description = "Plays an animation clip on the skeleton of the Entity's skinned mesh.",
               .docsAnchor = "manual/animation.md#the-animator",
               .requiredComponents = kRequires})
        .field(
            "skeleton", &Animator::skeleton,
            {.description = "The skeleton: a skin of a model (#skin/N).", .assetKind = AssetKind::Skeleton})
        .field("clip", &Animator::clip,
               {.description = "The clip (#animation/<name>); none shows the rest pose.",
                .assetKind = AssetKind::AnimationClip})
        .field(
            "graph", &Animator::graph,
            {.description = "An animation graph (.animgraph) that drives the Animator instead of the clip.",
             .assetKind = AssetKind::AnimationGraph})
        .field("speed", &Animator::speed,
               {.description = "Playback rate; negative plays backwards.", .softMin = -2.0, .softMax = 2.0})
        .field("time", &Animator::time,
               {.description = "Seconds into the clip.", .unit = Unit::Seconds, .min = 0.0})
        .field("loop", &Animator::loop,
               {.description = "Wrap at the clip's end; otherwise hold the last pose."})
        .field("playing", &Animator::playing, {.description = "Advance the time while the game runs."});
    reflect<BoneAttachment>("illusion.BoneAttachment", 1, registry)
        .meta({.displayName = "Bone attachment",
               .category = "Animation",
               .icon = "push_pin",
               .description = "Makes the Entity follow a joint of its parent's Animator (a socket).",
               .docsAnchor = "manual/animation.md#sockets",
               .requiredComponents = kRequiresParent})
        .field("joint", &BoneAttachment::joint, {.description = "The joint's name in the parent's skeleton."})
        .field("translation", &BoneAttachment::translation,
               {.description = "Offset in the joint's space.", .unit = Unit::Meters})
        .field("rotation", &BoneAttachment::rotation,
               {.description = "Offset rotation in the joint's space."})
        .field("scale", &BoneAttachment::scale, {.description = "Offset scale.", .step = 0.01});
    reflect<LookAt>("illusion.LookAt", 1, registry)
        .meta({.displayName = "Look at",
               .category = "Animation",
               .icon = "visibility",
               .description =
                   "Turns a joint chain of the Entity's Animator (a head and its neck) towards a target.",
               .docsAnchor = "manual/animation.md#look-at",
               .requiredComponents = kRequiresAnimator})
        .field("joint", &LookAt::joint, {.description = "The head joint."})
        .field("chainLength", &LookAt::chainLength,
               {.description = "Joints that share the turn: the head and up to three ancestors.",
                .min = 1.0,
                .max = 4.0})
        .field("target", &LookAt::target, {.description = "Entity to look at; none uses the point."})
        .field("point", &LookAt::point,
               {.description = "World-space point to look at without a target Entity.", .unit = Unit::Meters})
        .field("forward", &LookAt::forward, {.description = "The head's forward axis in its own space."})
        .field("maxAngle", &LookAt::maxAngle,
               {.description = "Largest turn from the animated direction.",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 3.14159265,
                .hint = FieldHint::Angle})
        .field("weight", &LookAt::weight,
               {.description = "Share of the turn; 0 turns it off.",
                .unit = Unit::Ratio,
                .min = 0.0,
                .max = 1.0})
        .field("speed", &LookAt::speed,
               {.description = "How fast the turn follows the target, per second; 0 follows at once.",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .hint = FieldHint::Angle});
    reflect<TwoBoneIk>("illusion.TwoBoneIk", 1, registry)
        .meta({.displayName = "Two-bone IK",
               .category = "Animation",
               .icon = "accessibility_new",
               .description = "Moves an end joint of the Animator (a foot, a hand) to a target with its two "
                              "parents.",
               .docsAnchor = "manual/animation.md#two-bone-ik"})
        .field("joint", &TwoBoneIk::joint,
               {.description = "The end joint; its parent and grandparent form the chain."})
        .field("target", &TwoBoneIk::target, {.description = "Entity to reach; none uses the point."})
        .field("point", &TwoBoneIk::point,
               {.description = "World-space target without a target Entity, or a point in the target "
                               "joint's space.",
                .unit = Unit::Meters})
        .field("targetJoint", &TwoBoneIk::targetJoint,
               {.description = "A joint of the same pose that the point moves with (the grip for the off "
                               "hand); empty uses the target Entity or a world-space point."})
        .field("pole", &TwoBoneIk::pole,
               {.description = "Entity the middle joint bends towards; none uses the pole point."})
        .field("polePoint", &TwoBoneIk::polePoint,
               {.description = "Pole in the Animator's model space without a pole Entity.",
                .unit = Unit::Meters})
        .field(
            "weight", &TwoBoneIk::weight,
            {.description = "Share of the IK; 0 turns it off.", .unit = Unit::Ratio, .min = 0.0, .max = 1.0})
        .field("alignToTarget", &TwoBoneIk::alignToTarget,
               {.description = "Also turn the end joint to the target Entity (or, with foot placement, the "
                               "ground; with a target joint, keep its animated turn relative to it)."});
    reflect<Aim>("illusion.Aim", 1, registry)
        .meta({.displayName = "Aim",
               .category = "Animation",
               .icon = "my_location",
               .description =
                   "Aims the Animator at a target: drives the graph's aim parameters and turns the "
                   "spine until the aim joint points at it.",
               .docsAnchor = "manual/animation.md#aim",
               .requiredComponents = kRequiresAnimator})
        .field("joint", &Aim::joint, {.description = "The aim joint (a hand, a weapon joint, the head)."})
        .field("forward", &Aim::forward, {.description = "The aim joint's aim axis in its own space."})
        .field("spine", &Aim::spine, {.description = "The top joint of the chain that turns."})
        .field("chainLength", &Aim::chainLength,
               {.description = "Joints that share the turn: the spine joint and up to three ancestors.",
                .min = 1.0,
                .max = 4.0})
        .field("weights", &Aim::weights,
               {.description = "Share of each chain joint, from the lowest up (normalized).",
                .unit = Unit::Ratio,
                .min = 0.0,
                .max = 1.0})
        .field("target", &Aim::target, {.description = "Entity to aim at; none uses the point."})
        .field("point", &Aim::point,
               {.description = "World-space point to aim at without a target Entity.", .unit = Unit::Meters})
        .field("maxYaw", &Aim::maxYaw,
               {.description = "Largest yaw from the character's forward.",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 3.14159265,
                .hint = FieldHint::Angle})
        .field("maxPitch", &Aim::maxPitch,
               {.description = "Largest pitch up or down.",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 1.57079633,
                .hint = FieldHint::Angle})
        .field("weight", &Aim::weight,
               {.description = "Share of the spine turn; 0 only drives the graph's parameters.",
                .unit = Unit::Ratio,
                .min = 0.0,
                .max = 1.0})
        .field("speed", &Aim::speed,
               {.description = "How fast the aim follows the target, per second; 0 follows at once.",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .hint = FieldHint::Angle})
        .field("yawParameter", &Aim::yawParameter,
               {.description = "Graph Float parameter that gets the yaw in radians; empty for none."})
        .field("pitchParameter", &Aim::pitchParameter,
               {.description = "Graph Float parameter that gets the pitch in radians; empty for none."});
    reflect<FootPlacement>("illusion.FootPlacement", 1, registry)
        .meta({.displayName = "Foot placement",
               .category = "Animation",
               .icon = "directions_walk",
               .description =
                   "Keeps the Animator's feet (its TwoBoneIk legs) on the ground while the game runs.",
               .docsAnchor = "manual/animation.md#foot-placement",
               .requiredComponents = kRequiresAnimator})
        .field("pelvis", &FootPlacement::pelvis,
               {.description = "The pelvis joint; the legs hang from it and it is lowered for the feet."})
        .field("rayLength", &FootPlacement::rayLength,
               {.description = "Ray length below the hip height.", .unit = Unit::Meters, .min = 0.0})
        .field("footHeight", &FootPlacement::footHeight,
               {.description = "Ankle height above the ground.", .unit = Unit::Meters, .min = 0.0})
        .field("maxStep", &FootPlacement::maxStep,
               {.description = "Largest height difference corrected.", .unit = Unit::Meters, .min = 0.0})
        .field("speed", &FootPlacement::speed,
               {.description = "How fast the corrections follow, per second; 0 follows at once.", .min = 0.0})
        .field("layerMask", &FootPlacement::layerMask,
               {.description = "Physics layers the rays hit (bit i = layer i)."});
    reflectStruct<SpringCollider>("illusion.SpringCollider", 1, registry.structs())
        .meta({.displayName = "Spring collider", .docsAnchor = "manual/animation.md#spring-bones"})
        .field("joint", &SpringCollider::joint,
               {.description = "Joint the sphere moves with; empty turns the entry off."})
        .field("offset", &SpringCollider::offset,
               {.description = "Centre in the joint's space.", .unit = Unit::Meters})
        .field("radius", &SpringCollider::radius,
               {.description = "Radius.", .unit = Unit::Meters, .min = 0.0});
    reflect<SpringBone>("illusion.SpringBone", 1, registry)
        .meta({.displayName = "Spring bone",
               .category = "Animation",
               .icon = "waves",
               .description =
                   "Lets a joint chain of the Animator (hair, a tail) swing with inertia and gravity.",
               .docsAnchor = "manual/animation.md#spring-bones",
               .requiredComponents = kRequiresAnimator})
        .field("root", &SpringBone::root,
               {.description = "First joint of the chain; every joint below it swings."})
        .field("stiffness", &SpringBone::stiffness,
               {.description = "Pull back towards the animated pose per step.",
                .unit = Unit::Ratio,
                .min = 0.0,
                .max = 1.0})
        .field("damping", &SpringBone::damping,
               {.description = "Velocity lost per step.", .unit = Unit::Ratio, .min = 0.0, .max = 1.0})
        .field("gravity", &SpringBone::gravity, {.description = "World-space acceleration in m/s²."})
        .field(
            "radius", &SpringBone::radius,
            {.description = "Radius of each chain joint for collisions.", .unit = Unit::Meters, .min = 0.0})
        .field("colliders", &SpringBone::colliders,
               {.description = "Spheres on joints of the same skeleton that the chain stays out of."});
    reflectRuntime<AnimationPose>("illusion.AnimationPose", {.displayName = "Animation pose"}, registry);
    reflectRuntime<FootPlacementPose>("illusion.FootPlacementPose", {.displayName = "Foot placement pose"},
                                      registry);
}

} // namespace illusion
