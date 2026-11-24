#pragma once

/// @file
/// The pose a virtual camera produces (docs/adr/0064-virtual-cameras.md) and the plain data its stages share.
/// Cameras look along their local -z axis with +y up.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// Where a camera is, where it looks and its lens.
struct CameraPose
{
    Vec3 position{0.0f};                   ///< World position.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World rotation without the Dutch angle.
    f32 fovY = 1.0471976f;                 ///< Vertical field of view in radians (60 degrees).
    f32 nearPlane = 0.1f;                  ///< Near clip distance in metres.
    f32 dutch = 0.0f; ///< Roll about the view axis in radians; positive turns the camera counter-clockwise.
};

/// The followed or looked-at object of a stage, in world space.
struct CameraTarget
{
    Vec3 position{0.0f};                   ///< World position.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World rotation.
};

/// What a body stage produces: the camera's position and the rotation used when no aim stage runs.
struct CameraPlacement
{
    Vec3 position{0.0f};                   ///< World position.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World rotation.
};

/// The pose's rotation with its Dutch angle applied: what the real camera's Transform gets.
[[nodiscard]] Quat cameraViewRotation(const CameraPose& pose) noexcept;

/// A rotation whose -z axis points along @p direction with no roll (+y up); @p fallback when @p direction is
/// zero or vertical.
[[nodiscard]] Quat cameraLookRotation(Vec3 direction, const Quat& fallback) noexcept;

} // namespace illusion
