#pragma once

/// @file
/// Blending between two camera poses (docs/adr/0064-virtual-cameras.md): the weight of a blend over time and
/// the blended pose.

#include <illusion/camera/CameraPose.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// How the weight of a blend changes over its duration.
enum class CameraBlendStyle : u8
{
    Cut,       ///< The new camera at once.
    Linear,    ///< Constant speed.
    EaseInOut, ///< Smoothstep: 3t^2 - 2t^3.
    Curve,     ///< A cubic Bezier from (0, 0) to (1, 1) through two control points, as CSS cubic-bezier().
};

/// A blend from one camera to another.
struct CameraBlend
{
    CameraBlendStyle style = CameraBlendStyle::EaseInOut; ///< The weight curve.
    f32 duration = 1.0f;                                  ///< Seconds; zero or less cuts.
    Vec2 control1{0.42f, 0.0f}; ///< First control point of Curve; x is clamped to [0, 1].
    Vec2 control2{0.58f, 1.0f}; ///< Second control point of Curve; x is clamped to [0, 1].
};

/// The weight in [0, 1] of the new camera @p elapsed seconds into @p blend.
[[nodiscard]] f32 cameraBlendWeight(const CameraBlend& blend, f32 elapsed) noexcept;

/// The y of the cubic Bezier (0, 0), @p control1, @p control2, (1, 1) at x = @p t (clamped to [0, 1]).
[[nodiscard]] f32 cubicBezierEase(Vec2 control1, Vec2 control2, f32 t) noexcept;

/// @p from and @p to mixed by @p t in [0, 1]: positions and lens values linearly, rotations by shortest-arc
/// slerp.
[[nodiscard]] CameraPose blendCameraPoses(const CameraPose& from, const CameraPose& to, f32 t) noexcept;

} // namespace illusion
