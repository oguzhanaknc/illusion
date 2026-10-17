#pragma once

/// @file
/// Post-processing settings of the SceneRenderer: screen-space ambient occlusion (GTAO), bloom and
/// temporal anti-aliasing, plus the sub-pixel jitter sequence used by TAA.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// Ground-truth ambient occlusion (GTAO) from the depth buffer, applied to indirect light.
struct AmbientOcclusionSettings
{
    bool enabled = true;  ///< Compute and apply screen-space ambient occlusion.
    f32 radius = 0.6f;    ///< World-space radius of the occlusion search.
    f32 intensity = 1.0f; ///< Exponent applied to the visibility; above 1 darkens.
};

/// Bloom: light from bright pixels spreading over the image, as in a real lens.
struct BloomSettings
{
    bool enabled = true;   ///< Add bloom.
    f32 intensity = 0.04f; ///< Fraction of the image replaced by its blurred copy (energy-preserving).
    u32 maxLevels = 6;     ///< Number of downsampled levels (wider bloom with more levels).
};

/// Temporal anti-aliasing: the camera jitters by a sub-pixel amount every frame and frames are
/// accumulated with reprojection.
struct TemporalAntiAliasingSettings
{
    bool enabled = false;          ///< Jitter and accumulate frames.
    f32 currentFrameWeight = 0.1f; ///< Weight of the newest frame (lower is smoother but slower to react).
};

/// All post-processing settings.
struct PostProcessSettings
{
    AmbientOcclusionSettings ambientOcclusion;         ///< Screen-space ambient occlusion.
    BloomSettings bloom;                               ///< Bloom.
    TemporalAntiAliasingSettings temporalAntiAliasing; ///< Temporal anti-aliasing.
};

/// Sub-pixel camera offset for frame @p frameIndex, in pixels within [-0.5, 0.5): the Halton (2, 3)
/// sequence, repeating every 8 frames.
[[nodiscard]] Vec2 temporalJitter(u32 frameIndex) noexcept;

/// @p projection with its image shifted by @p pixelOffset pixels on a target of @p size pixels (x right,
/// y down, as on screen).
[[nodiscard]] Mat4 jitterProjection(const Mat4& projection, Vec2 pixelOffset, Vec2 size) noexcept;

} // namespace illusion
