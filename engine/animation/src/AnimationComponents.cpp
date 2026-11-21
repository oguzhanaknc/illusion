#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/ecs/Reflection.hpp>

namespace illusion
{

namespace
{

constexpr std::string_view kRequires[] = {"illusion.Transform", "illusion.MeshRenderer"};
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
    reflectRuntime<AnimationPose>("illusion.AnimationPose", {.displayName = "Animation pose"}, registry);
}

} // namespace illusion
