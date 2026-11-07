#pragma once

/// @file
/// Shadow maps: cascades for the main directional light and cube shadow maps for point lights (ADR 0051),
/// with their settings and projection math.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>

namespace illusion
{

/// Maximum number of shadow cascades.
inline constexpr u32 kMaxShadowCascades = 4;

/// Maximum number of point lights that cast shadows in one frame.
inline constexpr u32 kMaxShadowedPointLights = 4;

/// Faces of a point light's cube shadow map, in the order +x, -x, +y, -y, +z, -z.
inline constexpr u32 kPointShadowFaces = 6;

/// Each cube face covers a little more than 90 degrees: tan(half field of view). The extra border (1/32 of
/// the face on each side) holds what the soft filter reads past the face's edge.
inline constexpr f32 kPointShadowFaceScale = 16.0f / 15.0f;

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
    /// Point lights with PointLight::castShadows that get a shadow map per frame (0 to
    /// kMaxShadowedPointLights); the ones nearest the camera win.
    u32 pointLightCount = kMaxShadowedPointLights;
    u32 pointResolution = 512; ///< Width and height of each cube face of a point light's shadow map.
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
/// perspectiveReversedZ() or orthographicReversedZ()) for a light travelling along @p lightDirection.
///
/// Each cascade bounds its frustum slice with a sphere, so its size does not change when the camera
/// turns, and its position is snapped to whole shadow-map texels, so shadow edges do not shimmer when
/// the camera moves.
[[nodiscard]] ShadowCascades computeShadowCascades(const Mat4& view, const Mat4& projection,
                                                   Vec3 lightDirection,
                                                   const ShadowSettings& settings) noexcept;

/// World to shadow clip space of cube @p face (see kPointShadowFaces) of a point light at @p position.
///
/// The projection is perspective with reversed, infinite depth (ADR 0013): depth is
/// `nearPlane / d`, where d is the distance along the face's axis, so it is 1 at @p nearPlane and falls
/// towards 0 far away. The field of view is a little wider than 90 degrees (kPointShadowFaceScale). Face
/// images are laid out like the faces of a Vulkan cube map, so `(x, y)` maps to the texel at
/// `(x * 0.5 + 0.5, 0.5 - y * 0.5)` as for the cascades.
[[nodiscard]] Mat4 pointShadowFaceMatrix(Vec3 position, u32 face, f32 nearPlane) noexcept;

/// The cube face (see kPointShadowFaces) that contains @p direction from the light: the axis with the
/// largest magnitude, positive axes first on ties.
[[nodiscard]] u32 pointShadowFace(Vec3 direction) noexcept;

} // namespace illusion
