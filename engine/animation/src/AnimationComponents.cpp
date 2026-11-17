#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/ecs/Reflection.hpp>

namespace illusion
{

namespace
{

constexpr std::string_view kRequires[] = {"illusion.Transform", "illusion.MeshRenderer"};

} // namespace

void registerAnimationComponents(ComponentRegistry& registry)
{
    reflect<Animator>("illusion.Animator", 1, registry)
        .meta({.displayName = "Animator",
               .category = "Animation",
               .icon = "directions_run",
               .description = "Plays an animation clip on the skeleton of the Entity's skinned mesh.",
               .docsAnchor = "manual/rendering.md#skeletal-animation",
               .requiredComponents = kRequires})
        .field(
            "skeleton", &Animator::skeleton,
            {.description = "The skeleton: a skin of a model (#skin/N).", .assetKind = AssetKind::Skeleton})
        .field("clip", &Animator::clip,
               {.description = "The clip (#animation/<name>); none shows the rest pose.",
                .assetKind = AssetKind::AnimationClip})
        .field("speed", &Animator::speed,
               {.description = "Playback rate; negative plays backwards.", .softMin = -2.0, .softMax = 2.0})
        .field("time", &Animator::time,
               {.description = "Seconds into the clip.", .unit = Unit::Seconds, .min = 0.0})
        .field("loop", &Animator::loop,
               {.description = "Wrap at the clip's end; otherwise hold the last pose."})
        .field("playing", &Animator::playing, {.description = "Advance the time while the game runs."});
    reflectRuntime<AnimationPose>("illusion.AnimationPose", {.displayName = "Animation pose"}, registry);
}

} // namespace illusion
