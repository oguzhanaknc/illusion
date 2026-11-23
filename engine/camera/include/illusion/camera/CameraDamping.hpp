#pragma once

/// @file
/// Frame-rate independent smoothing for camera stages (docs/adr/0064-virtual-cameras.md). Every helper takes
/// a smooth time in seconds; zero or less snaps to the target.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// Fraction of the remaining distance that exponential damping covers in @p dt: 1 - exp(-dt / smoothTime).
[[nodiscard]] f32 dampFactor(f32 smoothTime, f32 dt) noexcept;

/// Moves @p value towards @p target as a critically damped spring of angular frequency 2 / smoothTime. The
/// solution is exact, so one step of dt equals two steps of dt / 2; @p velocity is the spring's state.
void smoothDamp(f32& value, f32& velocity, f32 target, f32 smoothTime, f32 dt) noexcept;

/// smoothDamp() on each axis with its own smooth time.
void smoothDamp(Vec3& value, Vec3& velocity, Vec3 target, Vec3 smoothTime, f32 dt) noexcept;

/// Turns @p current towards @p target along the shortest arc by dampFactor(smoothTime, dt).
[[nodiscard]] Quat dampRotation(const Quat& current, const Quat& target, f32 smoothTime, f32 dt) noexcept;

/// @p radians wrapped to [-pi, pi).
[[nodiscard]] f32 wrapCameraAngle(f32 radians) noexcept;

} // namespace illusion
