#pragma once

/// @file
/// The Components of virtual cameras (docs/adr/0064-virtual-cameras.md): the saved VirtualCamera with its
/// body, aim and noise stages, CameraImpulse sources and the CameraBrain on the real Camera, and the runtime
/// states CameraWorld keeps on them.

#include <illusion/camera/CameraAim.hpp>
#include <illusion/camera/CameraBlend.hpp>
#include <illusion/camera/CameraBody.hpp>
#include <illusion/camera/CameraNoise.hpp>
#include <illusion/camera/CameraPose.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>
#include <string>
#include <vector>

namespace illusion
{

class ComponentRegistry;

/// A camera shot the CameraBrain can show. Its body stage (at most one of CameraFollow, CameraOrbit,
/// CameraSpringArm, CameraDolly) places it, its aim stage (CameraLookAt or CameraComposer) turns it and
/// CameraNoise shakes it; without a body it stands at its Entity's Transform, without an aim it keeps the
/// body's rotation. A socket is a BoneAttachment Entity (ADR 0061) used as follow or lookAt.
struct VirtualCamera
{
    i32 priority = 10;        ///< The brain shows the enabled camera with the highest priority.
    bool enabled = true;      ///< Can be shown; an Inactive Entity cannot either way.
    Entity follow;            ///< The body's target; null: the camera's own Entity.
    Entity lookAt;            ///< The aim's target; null: follow, or no aim when both are null.
    f32 fovY = 1.0471976f;    ///< Vertical field of view in radians (60 degrees).
    f32 nearPlane = 0.1f;     ///< Near clip distance in metres.
    f32 dutch = 0.0f;         ///< Roll about the view axis in radians.
    bool customBlend = false; ///< Blend to this camera with blendIn unless the brain has a pair override.
    CameraBlend blendIn{};    ///< The blend to this camera when customBlend.
};

/// Body stage: keeps the camera at an offset from the follow target (stepCameraFollow()).
struct CameraFollow
{
    Vec3 offset{0.0f, 2.0f, 5.0f};                       ///< From the target to the camera, in metres.
    CameraFollowSpace space = CameraFollowSpace::Target; ///< The space of offset and damping.
    Vec3 damping{0.1f};                                  ///< Smooth time per axis, in seconds.
    f32 rotationDamping = 0.1f;                          ///< Smooth time of the rotation, in seconds.
};

/// Body stage: turns the camera around the follow target by yaw and pitch (stepCameraOrbit()), driven by
/// input actions.
struct CameraOrbit
{
    f32 yaw = 0.0f;                     ///< Starting yaw about world +y, in radians.
    f32 pitch = -0.3490659f;            ///< Starting pitch in radians; positive looks up.
    f32 minPitch = -1.2217305f;         ///< Lowest pitch in radians.
    f32 maxPitch = 1.2217305f;          ///< Highest pitch in radians.
    bool limitYaw = false;              ///< Clamp yaw to [minYaw, maxYaw] instead of wrapping it.
    f32 minYaw = -3.1415927f;           ///< Lowest yaw when limitYaw.
    f32 maxYaw = 3.1415927f;            ///< Highest yaw when limitYaw.
    f32 distance = 5.0f;                ///< Starting distance from the pivot, in metres.
    f32 minDistance = 1.0f;             ///< Closest zoom, in metres.
    f32 maxDistance = 10.0f;            ///< Farthest zoom, in metres.
    Vec3 pivotOffset{0.0f, 1.5f, 0.0f}; ///< From the target to the pivot, in world axes.
    f32 damping = 0.05f;                ///< Smooth time of the angles, in seconds.
    /// Vector action that turns it: x yaws, y pitches; empty for none.
    std::string lookAction;
    Vec2 lookSpeed{3.0f, 2.0f}; ///< Radians per second at full deflection; negative inverts.
    std::string zoomAction;     ///< Axis action that changes the distance; empty for none.
    f32 zoomSpeed = 5.0f;       ///< Metres per second at full deflection.
};

/// Body stage: an arm from the follow target's shoulder to the camera that shortens to stay out of physics
/// geometry (stepCameraSpringArm()). The arm turns by its own yaw and pitch, relative to the target's yaw
/// when alignToTarget.
struct CameraSpringArm
{
    Vec3 shoulderOffset{0.5f, 1.6f, 0.0f}; ///< Side distance, height and back distance, in metres.
    f32 side = 1.0f;                       ///< -1 for the left shoulder, 1 for the right.
    f32 sideDamping = 0.2f;                ///< Smooth time of a shoulder swap, in seconds.
    f32 armLength = 3.0f;                  ///< Distance behind the shoulder, in metres.
    f32 collisionRadius = 0.2f;            ///< Radius of the cast sphere, in metres.
    u32 collisionLayers = 0xFFFFFFFFu;     ///< Physics layers the arm collides with (bit i = layer i).
    f32 pullInSpeed = 0.0f;                ///< Metres per second the arm shortens at; 0 is at once.
    f32 recoverySpeed = 4.0f;              ///< Metres per second the arm grows back at; 0 is at once.
    bool alignToTarget = true;             ///< yaw is relative to the target's facing (its -z axis).
    f32 yaw = 0.0f;                        ///< Starting yaw in radians.
    f32 pitch = -0.1745329f;               ///< Starting pitch in radians (-10 degrees); positive looks up.
    f32 minPitch = -1.2217305f;            ///< Lowest pitch in radians.
    f32 maxPitch = 1.2217305f;             ///< Highest pitch in radians.
    std::string lookAction;                ///< Vector action that turns it: x yaws, y pitches.
    Vec2 lookSpeed{3.0f, 2.0f};            ///< Radians per second at full deflection; negative inverts.
};

/// Body stage: moves the camera along a path (stepCameraDolly()). The points are in the space of the
/// camera's own Entity, so the track moves with it.
struct CameraDolly
{
    std::vector<CameraPathPoint> path;                 ///< The track: Hermite points and tangents.
    bool closed = false;                               ///< The last point joins the first.
    CameraDollyMode mode = CameraDollyMode::Parameter; ///< Where the goal position comes from.
    f32 position = 0.0f;                               ///< Goal path position in Parameter mode.
    f32 damping = 0.5f;                                ///< Smooth time of the path position, in seconds.
};

/// Aim stage: turns the camera to face the look-at target (stepCameraLookAt()).
struct CameraLookAt
{
    Vec3 targetOffset{0.0f}; ///< Added to the target's position, in world axes, in metres.
    f32 damping = 0.1f;      ///< Smooth time of the rotation, in seconds.
};

/// Aim stage: keeps the look-at target at a place on screen with dead and soft zones
/// (stepCameraComposer()).
struct CameraComposer
{
    Vec3 targetOffset{0.0f};    ///< Added to the target's position, in world axes, in metres.
    Vec2 screenPosition{0.0f};  ///< Where the target should be, from -1 (left, bottom) to 1.
    Vec2 deadZone{0.1f, 0.1f};  ///< Half size of the zone where the camera does not turn.
    Vec2 softZone{0.6f, 0.6f};  ///< Half size of the zone the target never leaves.
    Vec2 damping{0.25f, 0.25f}; ///< Smooth time of the horizontal and vertical correction, in seconds.
};

/// Handheld noise on top of the stages (sampleCameraNoise()).
struct CameraNoise
{
    Vec3 positionAmplitude{0.0f};   ///< Largest offset along the camera's x, y and z, in metres.
    Vec3 positionFrequency{1.0f};   ///< Base frequency per position axis, in hertz.
    Vec3 rotationAmplitude{0.005f}; ///< Largest pitch, yaw and roll, in radians.
    Vec3 rotationFrequency{0.5f};   ///< Base frequency per rotation axis, in hertz.
    f32 amplitudeGain = 1.0f;       ///< Scales every amplitude.
    f32 frequencyGain = 1.0f;       ///< Scales every frequency.
    u32 seed = 0;                   ///< Chooses the phases.
};

/// A source of camera shake at its Entity's position: every live camera within radius shakes, fading with
/// distance and over duration.
struct CameraImpulse
{
    f32 strength = 1.0f;     ///< Shake at the source; 1 is a strong hit.
    f32 radius = 10.0f;      ///< Distance where the shake fades to nothing, in metres.
    f32 duration = 0.5f;     ///< Seconds until it fades out.
    bool playOnStart = true; ///< Fire in the first update that sees it.
    f32 interval = 0.0f;     ///< Seconds between repeats after the first; 0 fires once.
};

/// A blend of the CameraBrain for one pair of cameras.
struct CameraBlendOverride
{
    Entity from;         ///< The camera blended from; null: any.
    Entity to;           ///< The camera blended to; null: any.
    CameraBlend blend{}; ///< The blend.
};

/// Shows the best VirtualCamera through the Camera on the same Entity: each update it writes the Camera's
/// world Transform and lens from the live virtual camera, blending when the live one changes. The blend of a
/// change is the first matching override, else the new camera's blendIn (VirtualCamera::customBlend), else
/// defaultBlend.
struct CameraBrain
{
    CameraBlend defaultBlend{};              ///< The blend without an override.
    std::vector<CameraBlendOverride> blends; ///< Per-pair blends, first match wins.
};

/// What CameraWorld keeps for a VirtualCamera (runtime only, never saved).
struct VirtualCameraState
{
    CameraPose pose;              ///< The pose of its last evaluation.
    u64 activation = 0;           ///< Order of the last time it became live-capable; breaks priority ties.
    u64 evaluated = 0;            ///< The CameraWorld update that last evaluated it; 0 for never.
    bool capable = false;         ///< Enabled and not Inactive at the last update.
    bool warned = false;          ///< A second body or aim was reported.
    CameraFollowState follow;     ///< State of CameraFollow.
    CameraOrbitState orbit;       ///< State of CameraOrbit.
    CameraSpringArmState arm;     ///< State of CameraSpringArm.
    Vec2 armAngles{0.0f};         ///< Yaw and pitch of CameraSpringArm, in radians.
    bool armAnglesSet = false;    ///< False until armAngles come from the settings.
    CameraDollyState dolly;       ///< State of CameraDolly.
    CameraLookAtState lookAt;     ///< State of CameraLookAt.
    CameraComposerState composer; ///< State of CameraComposer.
    CameraNoiseState noise;       ///< State of CameraNoise.
};

/// What CameraWorld keeps for a CameraBrain (runtime only, never saved).
struct CameraBrainState
{
    Entity live; ///< The camera shown, or null before any.
    Entity from; ///< The camera blended from; null when blending from fromPose or not blending.
    /// The pose blended from when from is null: the brain's pose when a blend broke off.
    CameraPose fromPose;
    CameraBlend blend{};   ///< The running blend.
    f32 elapsed = 0.0f;    ///< Seconds into the blend.
    bool blending = false; ///< A blend runs.
    f32 weight = 1.0f;     ///< Weight of the live camera in the last pose.
    CameraPose pose;       ///< The pose written to the Camera, shake included.
    bool hasPose = false;  ///< pose is set.
};

/// What CameraWorld keeps for a CameraImpulse (runtime only, never saved).
struct CameraImpulseState
{
    f32 untilNext = 0.0f; ///< Seconds until it fires again, with an interval.
    bool started = false; ///< An update has seen it.
};

/// Registers the saved camera Components (prefixed "illusion.", with the structs CameraBlend,
/// CameraPathPoint and CameraBlendOverride) and the runtime states with @p registry.
void registerCameraComponents(ComponentRegistry& registry);

} // namespace illusion

/// Names of CameraBlendStyle values in scene files.
template <>
struct illusion::EnumReflection<illusion::CameraBlendStyle>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Cut", 0}, illusion::EnumValue{"Linear", 1},
                                        illusion::EnumValue{"EaseInOut", 2}, illusion::EnumValue{"Curve", 3}};
};

/// Names of CameraFollowSpace values in scene files.
template <>
struct illusion::EnumReflection<illusion::CameraFollowSpace>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Target", 0}, illusion::EnumValue{"World", 1}};
};

/// Names of CameraDollyMode values in scene files.
template <>
struct illusion::EnumReflection<illusion::CameraDollyMode>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Parameter", 0},
                                        illusion::EnumValue{"NearestToTarget", 1}};
};
