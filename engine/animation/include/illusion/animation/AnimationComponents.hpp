#pragma once

/// @file
/// The Components of skeletal animation (docs/adr/0059-skeletal-animation.md,
/// docs/adr/0061-sockets-ik-and-ragdolls.md): the saved Animator, sockets, look-at, IK and foot placement,
/// and the runtime poses that AnimationWorld adds.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <array>
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

/// Turns a joint chain of the Entity's Animator towards a target (ADR 0061). Saved.
struct LookAt
{
    std::string joint;              ///< The head joint.
    u32 chainLength = 1;            ///< Joints that share the turn: the head and up to three ancestors (1-4).
    Entity target;                  ///< Entity to look at; null uses point.
    Vec3 point{0.0f};               ///< World-space point to look at when target is null.
    Vec3 forward{0.0f, 0.0f, 1.0f}; ///< The head's forward axis in its own space.
    f32 maxAngle = 1.2217305f;      ///< Largest turn in radians (70°).
    f32 weight = 1.0f;              ///< In [0, 1]; 0 turns the stage off.
    f32 speed = 6.2831853f;         ///< How fast the turn follows the target, in rad/s.
};

/// Moves an end joint (a foot, a hand) of an Animator to a target with its two parents (ADR 0061). It acts on
/// the Animator of its own Entity, else on its parent's; an Entity holds one, so further limbs go on children
/// of the Animator's Entity. Saved.
struct TwoBoneIk
{
    std::string joint;                ///< The end joint; its parent and grandparent form the chain.
    Entity target;                    ///< Entity whose position is the target; null uses point.
    Vec3 point{0.0f};                 ///< World-space target when target is null.
    Entity pole;                      ///< Entity the middle joint bends towards; null uses polePoint.
    Vec3 polePoint{0.0f, 0.0f, 1.0f}; ///< Pole in the Animator's model space when pole is null.
    f32 weight = 1.0f;                ///< In [0, 1]; 0 turns the limb's IK off.
    bool alignToTarget = false;       ///< Also give the end joint the target Entity's rotation.
};

/// Keeps the feet of the Entity's Animator on the ground (ADR 0061): before each Animation step the session
/// raycasts below every foot (a TwoBoneIk of the Animator whose chain hangs from the pelvis joint) and moves
/// the pelvis down to the lowest one. Saved.
struct FootPlacement
{
    std::string pelvis;          ///< The pelvis joint, lowered so that both feet can reach.
    f32 rayLength = 1.0f;        ///< Ray length below the hip height, in metres.
    f32 footHeight = 0.08f;      ///< Ankle height above the ground, in metres.
    f32 maxStep = 0.5f;          ///< Largest height difference corrected, in metres.
    f32 speed = 8.0f;            ///< How fast the corrections follow, per second.
    u32 layerMask = 0xFFFFFFFFu; ///< Physics layers the rays hit.
};

/// One foot of a FootPlacementPose.
struct FootContact
{
    Entity limb;                       ///< The Entity whose TwoBoneIk moves the foot.
    Vec3 animated{0.0f};               ///< World-space foot position of the last update, before IK.
    f32 lift = 0.0f;                   ///< Height the foot is moved by, in metres.
    Quat tilt{1.0f, 0.0f, 0.0f, 0.0f}; ///< World-space turn from +y to the ground normal.
    f32 weight = 0.0f;                 ///< Share of the foot's IK, in [0, 1].
};

/// The ground contact of a FootPlacement, added by AnimationWorld (runtime only, never saved): AnimationWorld
/// writes the animated placements, GameSession the lifts, tilts, weights and pelvis offset that the next
/// update applies. Without a session every weight stays 0 and the animated pose shows.
struct FootPlacementPose
{
    static constexpr u32 kMaxFeet = 4;        ///< Feet per Animator.
    std::array<FootContact, kMaxFeet> feet{}; ///< The first footCount are used.
    u32 footCount = 0;                        ///< Feet found by the last update.
    Vec3 pelvis{0.0f};       ///< World-space pelvis position of the last update, before the offset.
    f32 pelvisOffset = 0.0f; ///< Height the pelvis is moved by, in metres (zero or negative).
    u64 frame = 0;           ///< AnimationWorld::frame() of the update that wrote the animated placements.
};

/// Registers the saved Animator, BoneAttachment, LookAt, TwoBoneIk and FootPlacement (named
/// `illusion.Animator` and so on) and the runtime AnimationPose and FootPlacementPose with @p registry.
void registerAnimationComponents(ComponentRegistry& registry);

} // namespace illusion
