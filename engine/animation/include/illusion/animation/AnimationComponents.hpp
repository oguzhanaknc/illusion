#pragma once

/// @file
/// The Components of skeletal animation (docs/adr/0059-skeletal-animation.md): the saved Animator and the
/// runtime AnimationPose that AnimationWorld adds.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <string>

namespace illusion
{

class ComponentRegistry;

/// Plays a clip on the skeleton of the Entity's skinned MeshRenderer (AnimationWorld). Every field is saved,
/// so scenes and saved games keep where the clip was.
struct Animator
{
    AssetRef skeleton; ///< The skeleton: `model.glb#skin/N`.
    AssetRef clip;     ///< The clip: `model.glb#animation/<name>`; none plays the rest pose.
    /// An animation graph (`.animgraph`, ADR 0060) that drives the Animator instead of clip; none plays clip.
    AssetRef graph;
    f32 speed = 1.0f;    ///< Playback rate; negative plays backwards.
    f32 time = 0.0f;     ///< Seconds into the clip.
    bool loop = true;    ///< Wrap at the clip's end; otherwise hold the last pose.
    bool playing = true; ///< Advance time while the game runs (Play mode and the player).
};

/// The pose of an Animator, added and kept by AnimationWorld (runtime only, never saved).
struct AnimationPose
{
    u32 slot = 0;       ///< The pose's slot in its AnimationWorld.
    u32 jointCount = 0; ///< Joints of the skeleton; 0 while its assets load or when they failed.
    /// AnimationWorld::frame() of the last update that wrote the pose; the renderer skins only fresh poses.
    u64 frame = 0;
    Aabb bounds; ///< World-space bounds of the posed mesh (for culling).
    /// Root motion of the last update in world space (metres), applied by the session; zero without it.
    Vec3 rootTranslation{0.0f};
    f32 rootYaw = 0.0f; ///< Root motion rotation about +y of the last update, in radians.
};

/// Makes its Entity follow a joint of its parent's Animator (a socket, ADR 0061): AnimationWorld writes the
/// Entity's local transform from the joint's pose and this offset after every update. Saved.
struct BoneAttachment
{
    std::string joint;                     ///< Joint name in the parent's skeleton.
    Vec3 translation{0.0f};                ///< Offset in the joint's space.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Offset rotation in the joint's space.
    Vec3 scale{1.0f};                      ///< Offset scale.
};

/// Registers Animator (`illusion.Animator`) and BoneAttachment (`illusion.BoneAttachment`), both saved, and
/// AnimationPose (runtime) with @p registry.
void registerAnimationComponents(ComponentRegistry& registry);

} // namespace illusion
