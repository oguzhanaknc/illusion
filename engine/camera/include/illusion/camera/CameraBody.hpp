#pragma once

/// @file
/// Body stages of a virtual camera (docs/adr/0064-virtual-cameras.md): pure, allocation-free functions that
/// place the camera from plain settings, a per-camera state and this step's inputs. A state that is not yet
/// initialised snaps to the stage's goal on the first step.

#include <illusion/camera/CameraPose.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <optional>
#include <span>

namespace illusion
{

/// The space of CameraFollowSettings::offset.
enum class CameraFollowSpace : u8
{
    Target, ///< The target's rotation turns the offset; the camera takes the target's rotation.
    World,  ///< The offset is in world axes; the camera's rotation is the identity.
};

/// Settings of stepCameraFollow().
struct CameraFollowSettings
{
    Vec3 offset{0.0f, 2.0f, 5.0f};                       ///< From the target to the camera, in metres.
    CameraFollowSpace space = CameraFollowSpace::Target; ///< The space of offset and damping.
    Vec3 damping{0.1f};                                  ///< Smooth time per axis of that space, in seconds.
    f32 rotationDamping = 0.1f;                          ///< Smooth time of the rotation, in seconds.
};

/// Per-camera state of stepCameraFollow().
struct CameraFollowState
{
    Vec3 position{0.0f};                   ///< Last position.
    Vec3 velocity{0.0f};                   ///< Spring velocity in metres per second.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Last rotation.
    bool initialised = false;              ///< False until the first step.
};

/// Keeps the camera at an offset from the target, each axis damped as a critically damped spring.
[[nodiscard]] CameraPlacement stepCameraFollow(const CameraFollowSettings& settings, CameraFollowState& state,
                                               const CameraTarget& target, f32 dt) noexcept;

/// Settings of stepCameraOrbit(). Angles are in radians; positive pitch looks up.
struct CameraOrbitSettings
{
    f32 yaw = 0.0f;                     ///< Starting yaw about world +y.
    f32 pitch = -0.3490659f;            ///< Starting pitch (-20 degrees).
    f32 minPitch = -1.2217305f;         ///< Lowest pitch (-70 degrees).
    f32 maxPitch = 1.2217305f;          ///< Highest pitch (70 degrees).
    bool limitYaw = false;              ///< Clamp yaw to [minYaw, maxYaw] instead of wrapping it.
    f32 minYaw = -3.1415927f;           ///< Lowest yaw when limitYaw.
    f32 maxYaw = 3.1415927f;            ///< Highest yaw when limitYaw.
    f32 distance = 5.0f;                ///< Starting distance from the pivot, in metres.
    f32 minDistance = 1.0f;             ///< Closest zoom, in metres.
    f32 maxDistance = 10.0f;            ///< Farthest zoom, in metres.
    Vec3 pivotOffset{0.0f, 1.5f, 0.0f}; ///< From the target to the pivot, in world axes.
    f32 damping = 0.05f;                ///< Smooth time of the angles, in seconds.
};

/// This step's input of stepCameraOrbit(): the target and the look deltas.
struct CameraOrbitInput
{
    CameraTarget target; ///< The orbited object.
    f32 yaw = 0.0f;      ///< Yaw change in radians.
    f32 pitch = 0.0f;    ///< Pitch change in radians.
    f32 zoom = 0.0f;     ///< Distance change in metres.
};

/// Per-camera state of stepCameraOrbit().
struct CameraOrbitState
{
    f32 yaw = 0.0f;           ///< Goal yaw.
    f32 pitch = 0.0f;         ///< Goal pitch.
    f32 distance = 0.0f;      ///< Distance from the pivot.
    f32 smoothYaw = 0.0f;     ///< Damped yaw.
    f32 smoothPitch = 0.0f;   ///< Damped pitch.
    f32 yawVelocity = 0.0f;   ///< Spring velocity of the yaw, in radians per second.
    f32 pitchVelocity = 0.0f; ///< Spring velocity of the pitch, in radians per second.
    bool initialised =
        false; ///< False until the first step; the angles and distance then come from settings.
};

/// Turns the camera around a pivot above the target by yaw and pitch at a distance, looking at the pivot.
[[nodiscard]] CameraPlacement stepCameraOrbit(const CameraOrbitSettings& settings, CameraOrbitState& state,
                                              const CameraOrbitInput& input, f32 dt) noexcept;

/// Sphere casts against the physics world for the spring arm; implemented outside engine/camera.
class CameraCollisionQuery
{
public:
    /// Virtual destructor.
    virtual ~CameraCollisionQuery() = default;

    /// Distance the centre of a sphere of @p radius travels from @p origin along the unit @p direction before
    /// it touches a body on a layer in @p layerMask (bit i = layer i), or nothing within @p maxDistance.
    [[nodiscard]] virtual std::optional<f32> sphereCast(Vec3 origin, Vec3 direction, f32 maxDistance,
                                                        f32 radius, u32 layerMask) const noexcept = 0;

protected:
    CameraCollisionQuery() = default;
    CameraCollisionQuery(const CameraCollisionQuery&) = default;
    CameraCollisionQuery& operator=(const CameraCollisionQuery&) = default;
};

/// Settings of stepCameraSpringArm(). The arm starts on the target's vertical axis at the shoulder height and
/// ends at the camera: shoulderOffset.x times the side sideways, shoulderOffset.z plus armLength backwards.
struct CameraSpringArmSettings
{
    Vec3 shoulderOffset{0.5f, 1.6f, 0.0f}; ///< Side distance, height and back distance, in metres.
    f32 side = 1.0f;                       ///< -1 for the left shoulder, 1 for the right.
    f32 sideDamping = 0.2f;                ///< Smooth time of a shoulder swap, in seconds.
    f32 armLength = 3.0f;                  ///< Distance behind the shoulder, in metres.
    f32 collisionRadius = 0.2f;            ///< Radius of the cast sphere, in metres.
    u32 collisionLayers = 0xFFFFFFFFu;     ///< Physics layers the arm collides with (bit i = layer i).
    f32 pullInSpeed = 0.0f;   ///< Metres per second the arm shortens at; zero or less is at once.
    f32 recoverySpeed = 4.0f; ///< Metres per second the arm grows back at; zero or less is at once.
};

/// This step's input of stepCameraSpringArm().
struct CameraSpringArmInput
{
    CameraTarget target;                             ///< The followed object.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};           ///< World rotation of the arm and the camera.
    const CameraCollisionQuery* collision = nullptr; ///< Null: the arm never collides.
};

/// Per-camera state of stepCameraSpringArm().
struct CameraSpringArmState
{
    f32 side = 0.0f;          ///< Damped side.
    f32 sideVelocity = 0.0f;  ///< Spring velocity of the side, per second.
    f32 length = 0.0f;        ///< Current arm length from its start, in metres.
    bool initialised = false; ///< False until the first step.
};

/// Places the camera at the end of an arm from the target, shortened by one sphere cast so that it stays out
/// of geometry.
[[nodiscard]] CameraPlacement stepCameraSpringArm(const CameraSpringArmSettings& settings,
                                                  CameraSpringArmState& state,
                                                  const CameraSpringArmInput& input, f32 dt) noexcept;

/// A point of a dolly path, with the tangent of the cubic Hermite curve through it (per unit of path
/// position).
struct CameraPathPoint
{
    Vec3 position{0.0f}; ///< World position.
    Vec3 tangent{0.0f};  ///< Curve derivative at the point.
};

/// The range of path positions of @p points: [0, n - 1] for an open path, [0, n) for a closed one.
[[nodiscard]] f32 cameraPathEnd(std::span<const CameraPathPoint> points, bool closed) noexcept;

/// The point at path position @p at (segment index plus fraction; clamped, or wrapped when @p closed).
[[nodiscard]] Vec3 cameraPathPosition(std::span<const CameraPathPoint> points, bool closed, f32 at) noexcept;

/// The curve derivative at path position @p at.
[[nodiscard]] Vec3 cameraPathTangent(std::span<const CameraPathPoint> points, bool closed, f32 at) noexcept;

/// The path position nearest to @p point; the first one on ties.
[[nodiscard]] f32 nearestCameraPathPosition(std::span<const CameraPathPoint> points, bool closed,
                                            Vec3 point) noexcept;

/// Where stepCameraDolly() takes its goal position on the path from.
enum class CameraDollyMode : u8
{
    Parameter,       ///< CameraDollySettings::position.
    NearestToTarget, ///< The path position nearest to the target.
};

/// Settings of stepCameraDolly().
struct CameraDollySettings
{
    CameraDollyMode mode = CameraDollyMode::Parameter; ///< The goal position's source.
    f32 position = 0.0f;                               ///< Goal path position in Parameter mode.
    bool closed = false;                               ///< The last point joins the first.
    f32 damping = 0.5f;                                ///< Smooth time of the path position, in seconds.
};

/// This step's input of stepCameraDolly().
struct CameraDollyInput
{
    std::span<const CameraPathPoint> path; ///< The track.
    CameraTarget target;                   ///< The object of NearestToTarget.
};

/// Per-camera state of stepCameraDolly().
struct CameraDollyState
{
    f32 position = 0.0f;      ///< Current path position.
    f32 velocity = 0.0f;      ///< Spring velocity of the path position, per second.
    bool initialised = false; ///< False until the first step.
};

/// Moves the camera along a path to the goal position; the camera faces along the path. An empty path places
/// it at the target.
[[nodiscard]] CameraPlacement stepCameraDolly(const CameraDollySettings& settings, CameraDollyState& state,
                                              const CameraDollyInput& input, f32 dt) noexcept;

} // namespace illusion
