#pragma once

/// @file
/// Aim stages of a virtual camera (docs/adr/0064-virtual-cameras.md): pure, allocation-free functions that
/// turn the camera placed by its body stage towards a target. Neither adds roll; the Dutch angle comes after.

#include <illusion/camera/CameraPose.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <optional>

namespace illusion
{

/// This step's input of an aim stage.
struct CameraAimInput
{
    Vec3 cameraPosition{0.0f}; ///< Where the body stage placed the camera.
    /// The body's rotation, used while the target gives no direction.
    Quat bodyRotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 targetPosition{0.0f}; ///< The looked-at point.
};

/// Settings of stepCameraLookAt().
struct CameraLookAtSettings
{
    f32 damping = 0.1f; ///< Smooth time of the rotation, in seconds.
};

/// Per-camera state of stepCameraLookAt().
struct CameraLookAtState
{
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Last rotation.
    bool initialised = false;              ///< False until the first step.
};

/// Turns the camera to face the target, damped.
[[nodiscard]] Quat stepCameraLookAt(const CameraLookAtSettings& settings, CameraLookAtState& state,
                                    const CameraAimInput& input, f32 dt) noexcept;

/// Settings of stepCameraComposer(). Screen positions run from -1 (left, bottom) to 1 (right, top).
struct CameraComposerSettings
{
    Vec2 screenPosition{0.0f}; ///< Where the target should be on screen.
    /// Half size of the zone around screenPosition where the camera does not turn.
    Vec2 deadZone{0.1f, 0.1f};
    Vec2 softZone{0.6f, 0.6f};  ///< Half size of the zone the target never leaves; at least deadZone.
    Vec2 damping{0.25f, 0.25f}; ///< Smooth time of the horizontal and vertical correction, in seconds.
};

/// This step's input of stepCameraComposer().
struct CameraComposerInput
{
    CameraAimInput aim;      ///< Camera and target.
    f32 fovY = 1.0471976f;   ///< Vertical field of view in radians.
    f32 aspect = 1.7777778f; ///< Width over height of the view.
};

/// Per-camera state of stepCameraComposer().
struct CameraComposerState
{
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Last rotation.
    bool initialised = false;              ///< False until the first step.
};

/// Where @p point appears on the screen of a camera at @p position with @p rotation, or nothing when it is
/// not in front of the camera.
[[nodiscard]] std::optional<Vec2> cameraScreenPosition(Vec3 position, const Quat& rotation, f32 fovY,
                                                       f32 aspect, Vec3 point) noexcept;

/// Turns the camera so that the target sits in the composition: inside the dead zone the camera holds still,
/// inside the soft zone it turns towards the dead zone's edge damped, and a target beyond the soft zone is
/// first brought to its edge at once. A target behind the camera snaps to screenPosition.
[[nodiscard]] Quat stepCameraComposer(const CameraComposerSettings& settings, CameraComposerState& state,
                                      const CameraComposerInput& input, f32 dt) noexcept;

} // namespace illusion
