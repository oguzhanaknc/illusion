#pragma once

/// @file
/// Cascaded shadow maps for the main directional light: settings and the cascade fitting math.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>

namespace illusion
{

/// Maximum number of shadow cascades.
inline constexpr u32 kMaxShadowCascades = 4;

/// Shadow quality settings of a SceneRenderer.
struct ShadowSettings
{
    bool enabled = true;         ///< Render shadows for the first DirectionalLight with castShadows.
    u32 cascadeCount = 4;        ///< Number of cascades (1 to kMaxShadowCascades).
    u32 resolution = 2048;       ///< Width and height of each cascade's shadow map.
    f32 maxDistance = 80.0f;     ///< View distance where shadows end (they fade out over the last 10%).
    f32 splitLambda = 0.8f;      ///< Blend between uniform (0) and logarithmic (1) cascade splits.
    f32 casterDistance = 100.0f; ///< How far towards the light, beyond a cascade, casters are included.
    f32 depthBias = 0.02f;       ///< Receiver depth bias in world units.
    f32 normalBias = 1.5f;       ///< Receiver offset along its normal, in shadow-map texels.
    f32 slopeBias = 2.0f;        ///< Slope-scaled depth bias applied when rendering casters.
};

/// One cascade: an orthographic light-space box covering a slice of the view frustum.
struct ShadowCascade
{
    Mat4 viewProjection{1.0f}; ///< World to shadow clip space (x, y in [-1, 1], depth 0 nearest the light).
    f32 splitNear = 0.0f;      ///< Start of the slice, as distance along the camera's forward axis.
    f32 splitFar = 0.0f;       ///< End of the slice.
    f32 worldSize = 0.0f;      ///< Width (and height) of the box in world units.
    f32 depthRange = 0.0f;     ///< Depth of the box in world units.
};

/// The cascades for one view.
struct ShadowCascades
{
    std::array<ShadowCascade, kMaxShadowCascades> cascades{}; ///< Cascades, nearest first.
    u32 count = 0;                                            ///< Number of valid cascades.
};

/// Fits cascades to the view described by @p view (world to view) and @p projection (from
/// perspectiveReversedZ()) for a light travelling along @p lightDirection.
///
/// Each cascade bounds its frustum slice with a sphere, so its size does not change when the camera
/// turns, and its position is snapped to whole shadow-map texels, so shadow edges do not shimmer when
/// the camera moves.
[[nodiscard]] ShadowCascades computeShadowCascades(const Mat4& view, const Mat4& projection,
                                                   Vec3 lightDirection,
                                                   const ShadowSettings& settings) noexcept;

} // namespace illusion
