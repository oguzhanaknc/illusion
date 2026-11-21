#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/ecs/Reflection.hpp>

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
               {.description = "World-space target without a target Entity.", .unit = Unit::Meters})
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
                               "ground)."});
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
    reflectRuntime<AnimationPose>("illusion.AnimationPose", {.displayName = "Animation pose"}, registry);
    reflectRuntime<FootPlacementPose>("illusion.FootPlacementPose", {.displayName = "Foot placement pose"},
                                      registry);
}

} // namespace illusion
