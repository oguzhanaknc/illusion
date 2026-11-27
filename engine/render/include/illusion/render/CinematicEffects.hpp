#pragma once

/// @file
/// Cinematic post effects (docs/adr/0067-cinematic-post-effects.md): the saved DepthOfField and MotionBlur
/// singletons, the thin-lens and shutter math the renderer, the editor and tests share, and the quality
/// levels that set their sample counts. This header needs no GPU headers.
///
/// @code
/// world.create(illusion::DepthOfField{.bladeCount = 6});
/// world.create(illusion::MotionBlur{.cameraBlur = 0.5f});
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>

namespace illusion
{

struct Camera;

/// Where the focus distance of a DepthOfField comes from.
enum class FocusMode : u8
{
    /// Camera::focusDistance, which a VirtualCamera with a focus target keeps on that target.
    Manual,
    /// The scene depth at DepthOfField::focusPoint, metered on the GPU and followed at focusSpeed.
    ScreenPoint,
};

/// Depth of field (reflected as "illusion.DepthOfField", one per scene): surfaces in front of and behind the
/// camera's focus distance blur by the circle of confusion of its lens (Camera::fStop, Camera::sensorHeight
/// and the field of view). Without an active one nothing blurs; orthographic cameras never blur.
struct DepthOfField
{
    FocusMode focusMode = FocusMode::Manual; ///< Where the focus distance comes from.
    /// Image point the ScreenPoint mode focuses on: (0, 0) top left, (1, 1) bottom right.
    Vec2 focusPoint{0.5f, 0.5f};
    f32 focusSpeed = 4.0f; ///< How fast the ScreenPoint focus follows the depth, per second.
    /// Largest blur radius as a fraction of the image height, 0.002 to 0.05 (0.015: 16 px at 1080p).
    f32 maxBlur = 0.015f;
    f32 nearScale = 1.0f; ///< Multiplies the blur in front of the focus distance (0: none).
    f32 farScale = 1.0f;  ///< Multiplies the blur behind the focus distance (0: none).
    u32 bladeCount = 0;   ///< Aperture blades shaping the bokeh: 0 for a circle, else 3 to 9 for a polygon.
    f32 bladeRotation = 0.0f; ///< Turn of the bokeh polygon in radians.
    f32 bokehAspect = 1.0f;   ///< Width over height of the bokeh, 0.25 to 4: below 1 gives anamorphic ovals.
};

/// Per-object and camera motion blur (reflected as "illusion.MotionBlur", one per scene): each pixel blurs
/// along its motion during the time the shutter is open (Camera::shutterAngle). Without an active one, or on
/// the first frame after a camera cut, nothing blurs.
struct MotionBlur
{
    f32 intensity = 1.0f; ///< Multiplies the blur of the shutter (0: none).
    /// Share of the blur from the camera's own motion: 0 only moving objects blur, 1 all.
    f32 cameraBlur = 1.0f;
    /// Longest blur radius as a fraction of the image height, 0.005 to 0.1 (0.04: 43 px at 1080p).
    f32 maxBlur = 0.04f;
};

/// Quality level of the render effects (RenderSettings::quality); postQualityOf() gives its sample counts.
enum class RenderQuality : u8
{
    Low,    ///< Fewest samples, no lens flares.
    Medium, ///< Fewer samples.
    High,   ///< The default.
    Ultra,  ///< Most samples.
};

/// Sample counts of the cinematic passes, set by a RenderQuality (PostProcessSettings::quality).
struct PostQualitySettings
{
    u32 depthOfFieldRings = 4;      ///< Rings of the half-resolution gather, 1 to 6 (depthOfFieldSamples()).
    bool depthOfFieldMedian = true; ///< Filter the half-resolution blur with a 3x3 median before compositing.
    u32 motionBlurSamples = 12;     ///< Taps per pixel along the motion, an even number from 4 to 32.
    bool lensFlares = true; ///< Draw the ghosts and halo of PostEffects; lens dirt is drawn either way.
};

/// The sample counts of @p quality (the table of ADR 0067).
[[nodiscard]] PostQualitySettings postQualityOf(RenderQuality quality) noexcept;

/// Taps per half-resolution pixel of a depth of field gather with @p rings rings: the centre and 8 per ring
/// index.
[[nodiscard]] constexpr u32 depthOfFieldSamples(u32 rings) noexcept
{
    return 1u + 4u * rings * (rings + 1u);
}

/// A thin lens in metres, the model of the circle of confusion.
struct ThinLens
{
    f32 focalLength = 0.0207846f; ///< Focal length in metres (60 degrees vertical on a 24 mm sensor).
    f32 fStop = 5.6f;             ///< Focal length over the aperture's diameter.
    f32 focusDistance = 10.0f;    ///< Distance in metres along the view axis that is in focus.
    f32 sensorHeight = 0.024f;    ///< Height in metres of the image on the sensor.
};

/// Nearest and farthest view distance in metres that a lens shows sharp.
struct FocusRange
{
    f32 nearDistance = 0.0f; ///< Nearest sharp distance.
    f32 farDistance = 0.0f;  ///< Farthest sharp distance; infinity beyond the hyperfocal distance.
};

/// Focal length in metres of a lens that shows the vertical field of view @p fovY (radians) on a sensor
/// @p sensorHeight metres high.
[[nodiscard]] f32 focalLengthOf(f32 fovY, f32 sensorHeight) noexcept;

/// The thin lens of a perspective @p camera, with the focus distance kept beyond the focal length.
[[nodiscard]] ThinLens thinLensOf(const Camera& camera) noexcept;

/// Signed radius of the circle of confusion at view distance @p depth (metres) as a fraction of the image
/// height: negative in front of the focus distance, positive behind it, 0 at it.
[[nodiscard]] f32 circleOfConfusion(const ThinLens& lens, f32 depth) noexcept;

/// The distances @p lens shows with a circle of confusion radius at most @p sharpRadius (fraction of the
/// image height).
[[nodiscard]] FocusRange focusRange(const ThinLens& lens, f32 sharpRadius) noexcept;

/// Fraction of the frame interval a shutter of @p shutterAngle radians is open, in [0, 1] (pi gives 0.5).
[[nodiscard]] f32 shutterFraction(f32 shutterAngle) noexcept;

/// Side in pixels of the motion blur tiles for a blur radius of @p maxBlur (fraction of the image height) on
/// an image @p imageHeight pixels high: the radius rounded up to a multiple of 8, from 8 to 128.
[[nodiscard]] u32 motionBlurTileSize(f32 maxBlur, u32 imageHeight) noexcept;

} // namespace illusion

/// Stable scene-file names of focus modes.
template <>
struct illusion::EnumReflection<illusion::FocusMode>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Manual", 0},
                                        illusion::EnumValue{"ScreenPoint", 1}};
};

/// Stable settings-file names of render quality levels.
template <>
struct illusion::EnumReflection<illusion::RenderQuality>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Low", 0}, illusion::EnumValue{"Medium", 1},
                                        illusion::EnumValue{"High", 2}, illusion::EnumValue{"Ultra", 3}};
};
