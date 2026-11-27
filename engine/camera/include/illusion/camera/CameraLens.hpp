#pragma once

/// @file
/// The lens of a camera pose beyond its field of view (docs/adr/0067-cinematic-post-effects.md): focus,
/// aperture and shutter, blended between virtual cameras and written to the real Camera by CameraWorld.
/// CameraPose::lens holds it.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

struct CameraPose;

/// Focus, aperture and shutter of a camera, in the units of the Camera Component.
struct CameraLens
{
    f32 focusDistance = 10.0f;     ///< Distance in metres along the view axis that is in focus.
    f32 fStop = 5.6f;              ///< Focal length over the aperture's diameter.
    f32 shutterAngle = 3.1415927f; ///< Shutter angle in radians (pi: open half of each frame).
};

/// The lens at @p weight between @p from (0) and @p to (1): the focus blended in dioptres (one over the
/// distance), so a pull from near to far passes through the middle distances evenly; the aperture in stops
/// (log2 of the f-number); the shutter angle linearly.
[[nodiscard]] CameraLens blendCameraLenses(const CameraLens& from, const CameraLens& to, f32 weight) noexcept;

/// Distance in metres from @p pose to @p point along the pose's view axis, at least the pose's near plane.
[[nodiscard]] f32 focusDistanceTo(const CameraPose& pose, Vec3 point) noexcept;

} // namespace illusion
