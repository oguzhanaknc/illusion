#pragma once

/// @file
/// The render Components that scenes save: what to draw and how to light it, described by asset references
/// and plain values. This header needs no GPU headers, so tools and game code can author scenes without the
/// renderer.
///
/// The renderer itself draws the runtime Components RenderMesh and RenderSky (Components.hpp). A
/// RenderAssetResolver turns MeshRenderer and SkyLight into them every frame, loading the assets as needed;
/// code that builds geometry at run time writes RenderMesh directly.
///
/// @code
/// world.create(illusion::Transform{}, illusion::MeshRenderer{
///     .mesh = illusion::AssetRef("game:/models/ship.glb#mesh/0"),
///     .material = illusion::AssetRef("game:/materials/hull.material")});
/// world.create(illusion::Transform{}, illusion::SkyLight{.environment =
/// illusion::AssetRef("game:/sky.hdr")});
/// @endcode

#include <illusion/assets/Environment.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <cmath>
#include <numbers>

namespace illusion
{

/// Draws one submesh of a mesh asset with a material asset (reflected as "illusion.MeshRenderer").
struct MeshRenderer
{
    /// The mesh: `model.glb#mesh/N`, `model.glb#node/<name>` or `builtin:/mesh/...`. A model path without a
    /// fragment means its first mesh.
    AssetRef mesh{};
    u32 submesh = 0; ///< Submesh index.
    /// The material: a `.material` file, `model.glb#material/N` or `builtin:/material/default`. Empty uses
    /// the material the model assigns to the submesh (the default material for built-in meshes).
    AssetRef material{};
    u32 layers = 1;          ///< Render layer bits, for cameras and passes that select layers.
    bool castShadows = true; ///< Drawn into shadow maps.
    bool visible = true;     ///< Drawn at all; hidden renderers keep their assets loaded.
};

/// Projection used by a Camera.
enum class CameraProjection : u8
{
    Perspective,  ///< Perspective with an infinite reversed-Z far plane.
    Orthographic, ///< Parallel rays with a finite reversed-Z far plane.
};

/// Camera (reflected as "illusion.Camera" v2), looking through the Transform's -z axis.
struct Camera
{
    f32 fovY = 1.0471976f; ///< Vertical field of view in radians (60 degrees).
    f32 nearPlane = 0.1f;  ///< Positive near clip distance; perspective projection has an infinite far plane.
    f32 exposure = 1.0f;   ///< Linear exposure multiplier applied before tonemapping.
    CameraProjection projection = CameraProjection::Perspective; ///< Projection mode.
    f32 orthoHeight = 10.0f; ///< Vertical extent in world units for orthographic projection.
    f32 farPlane = 1000.0f;  ///< Orthographic far distance; must exceed nearPlane.
    i32 priority = 0;        ///< Higher priorities win findMainCamera(); ties use the lowest Entity index.
};

/// Sun-like light (reflected as "illusion.DirectionalLight"). It shines along the Transform's -z axis.
struct DirectionalLight
{
    Vec3 color{1.0f};        ///< Linear colour.
    f32 intensity = 3.0f;    ///< Illuminance multiplier.
    bool castShadows = true; ///< The first DirectionalLight with this set casts shadows.
    f32 angularDiameter =
        0.0093f; ///< Apparent size in radians (the sun: 0.0093); larger gives softer shadows.
};

/// Omnidirectional light at the Transform's position (reflected as "illusion.PointLight").
struct PointLight
{
    Vec3 color{1.0f};      ///< Linear colour.
    f32 intensity = 10.0f; ///< Luminous intensity multiplier.
    f32 range = 10.0f;     ///< Distance at which the light fades to zero.
    /// Cast shadows in every direction (ADR 0051). Up to ShadowSettings::pointLightCount such lights get
    /// a shadow map each frame, the ones nearest the camera first; the others light without shadows.
    bool castShadows = false;
    f32 sourceRadius = 0.05f;   ///< Size of the glowing body in metres; larger gives softer shadows.
    f32 shadowBias = 0.03f;     ///< Receiver depth bias in metres; raise it if surfaces shadow themselves.
    f32 shadowNearPlane = 0.1f; ///< Casters closer to the light than this (a lamp's housing) are ignored.
};

/// Image-based lighting and background from an environment (reflected as "illusion.SkyLight"). The
/// renderer uses the first SkyLight whose environment is loaded; the Transform's rotation turns the
/// environment (position and scale are ignored).
struct SkyLight
{
    /// An equirectangular image (usually `.hdr`), or `builtin:/sky/procedural` for the ProceduralSky of the
    /// same Entity (its defaults if it has none). Empty leaves the Entity's RenderSky to code.
    AssetRef environment{};
    f32 intensity = 1.0f;   ///< Multiplier for the environment's radiance.
    bool background = true; ///< Draw the environment behind the scene instead of the clear colour.
};

/// Parameters of `builtin:/sky/procedural` for the SkyLight of the same Entity (reflected as
/// "illusion.ProceduralSky"); the fields are those of SkyDesc.
struct ProceduralSky
{
    Vec3 zenith{0.25f, 0.45f, 0.9f};       ///< Linear radiance straight up.
    Vec3 horizon{0.9f, 0.95f, 1.0f};       ///< Linear radiance at the horizon.
    Vec3 ground{0.25f, 0.22f, 0.2f};       ///< Linear radiance below the horizon.
    Vec3 sunDirection{0.3f, 0.6f, -0.75f}; ///< Direction towards the sun (normalized internally).
    Vec3 sunRadiance{0.0f};                ///< Sun disc radiance; zero for no disc (use a DirectionalLight).
    f32 sunAngularRadius = 0.02f;          ///< Sun disc radius in radians.
    u32 resolution = 512;                  ///< Width of the generated equirectangular image in pixels.

    /// The sky as a SkyDesc for makeSkyImage().
    [[nodiscard]] SkyDesc desc() const noexcept
    {
        return SkyDesc{zenith, horizon, ground, sunDirection, sunRadiance, sunAngularRadius};
    }
};

/// Curve that maps HDR colours to the display (PostEffects::tonemapper).
enum class Tonemapper : u8
{
    Aces, ///< ACES filmic fit: punchy contrast; very bright saturated colours shift towards yellow and white.
    AgX,  ///< AgX: keeps hues of bright saturated light (neon, fire) and fades them to white gracefully.
};

/// How the camera turns light into the image (reflected as "illusion.PostEffects", one per scene): automatic
/// exposure that adapts like an eye, the tonemapping curve, colour grading and lens effects. Without it the
/// renderer uses Camera::exposure and ACES with no grading.
struct PostEffects
{
    bool autoExposure = true;        ///< Meter the image and adapt the exposure to it.
    f32 exposureCompensation = 0.0f; ///< Stops (EV) added to the exposure: +1 doubles the brightness.
    f32 minExposure = -2.0f;    ///< Darkest automatic exposure in stops: a bright scene is not darkened more.
    f32 maxExposure = 2.5f;     ///< Brightest automatic exposure in stops: a dark night stays dark.
    f32 adaptationSpeed = 1.5f; ///< How fast the exposure follows the light, per second.
    Tonemapper tonemapper = Tonemapper::AgX; ///< Curve from HDR to the display.
    f32 contrast = 1.0f;    ///< Contrast around middle grey: above 1 deepens shadows and lifts lights.
    f32 saturation = 1.0f;  ///< Colourfulness: 0 grey, 1 unchanged, above 1 more vivid.
    f32 temperature = 0.0f; ///< White balance: negative cooler (blue), positive warmer (orange), -1 to 1.
    f32 tint = 0.0f;        ///< White balance: negative greener, positive more magenta, -1 to 1.
    f32 vignette = 0.0f;    ///< Darkening of the corners, 0 to 1.
    f32 grain = 0.0f;       ///< Strength of animated film grain, 0 to 1.
    f32 chromaticAberration = 0.0f; ///< Colour fringes at the corners, in pixels.
};

/// Light-scattering fog in the air (reflected as "illusion.VolumetricFog", one per scene, ADR 0050): thick
/// near the ground and thinning with height, it glows where the sun, the moon or a point light shines
/// through it and stays dark in their shadows, so light shafts appear behind trees and around lamps. Fog
/// covers opaque surfaces, the sky, blended surfaces and particles up to `maxDistance`; the Wind carries its
/// drifting noise.
struct VolumetricFog
{
    f32 density = 0.02f; ///< Extinction per metre at the base height: 0.02 hides half the light in 35 m.
    f32 heightFalloff = 0.15f;     ///< How fast the fog thins above the base height, per metre (0: uniform).
    f32 baseHeight = 0.0f;         ///< Height in metres below which the fog has its full density.
    Vec3 albedo{0.9f, 0.9f, 0.9f}; ///< Colour of the light the fog scatters (1: none absorbed).
    f32 anisotropy = 0.5f;         ///< Henyey-Greenstein g, -0.9 to 0.9: positive glows towards the lights.
    f32 lightIntensity = 1.0f;     ///< Multiplies the sun, moon and point light scattered by the fog.
    f32 ambientIntensity = 1.0f;   ///< Multiplies the sky light scattered by the fog.
    f32 maxDistance = 96.0f; ///< Metres from the camera the fog volume reaches; beyond it the fog stops.
    f32 noiseAmount = 0.5f;  ///< How patchy the fog is, 0 (even) to 1 (clear gaps between wisps).
    f32 noiseSize = 12.0f;   ///< Size in metres of the patches.
    f32 windFactor = 1.0f;   ///< How fast the Wind carries the patches (1: at the wind speed).
};

/// A sky that follows the time of day (reflected as "illusion.SkyAtmosphere", ADR 0049): air and haze
/// scattering for any sun height, a sun disc, a moon with its phase, and stars at night. Its sun or moon is
/// the scene's main light: the renderer adds it as a shadowed directional light, and lights the scene with
/// the sky. It replaces any SkyLight. Change `timeOfDay` to move the sun; nothing is baked.
struct SkyAtmosphere
{
    f32 timeOfDay = 10.0f; ///< Hours since midnight, 0 to 24: the sun rises at 6 and sets at 18.
    f32 sunAzimuth = 0.0f; ///< Turn of the sun's path about +y in radians (0: it rises at +x and sets at -x).
    f32 sunTilt = 0.6f;    ///< Angle in radians of the noon sun from straight up, towards -z.
    f32 moonOffset = 0.0f; ///< Hours the moon trails the point opposite the sun: 0 full moon, 12 new moon.
    f32 turbidity = 2.5f;  ///< Haze: 1 clear mountain air, 10 thick haze.
    f32 skyIntensity = 1.0f;   ///< Multiplies the sky's radiance and its image-based lighting.
    f32 sunIntensity = 4.0f;   ///< Illuminance of the sunlight at noon (as DirectionalLight::intensity).
    f32 moonIntensity = 0.15f; ///< Illuminance of a full moon's light.
    f32 starIntensity = 1.0f;  ///< Brightness of the stars at night (0: none).
    Vec3 groundAlbedo{0.2f, 0.18f, 0.15f}; ///< Colour of the ground seen below the horizon.
    bool castShadows = true;               ///< The sun or moon light casts shadows.
    u32 lightingResolution = 64; ///< Face size of the reflection cube map (a power of two, 16 to 256).

    /// Direction towards the sky body on the sun's path at @p hours (the sun at timeOfDay, the moon 12 hours
    /// and moonOffset later).
    [[nodiscard]] Vec3 pathDirection(f32 hours) const noexcept
    {
        const f32 angle = (hours - 12.0f) / 24.0f * 2.0f * std::numbers::pi_v<f32>;
        const Vec3 local(-std::sin(angle), std::cos(angle) * std::cos(sunTilt),
                         -std::cos(angle) * std::sin(sunTilt));
        const f32 c = std::cos(sunAzimuth);
        const f32 s = std::sin(sunAzimuth);
        return Vec3(local.x * c + local.z * s, local.y, -local.x * s + local.z * c);
    }
    /// Direction towards the sun.
    [[nodiscard]] Vec3 sunDirection() const noexcept { return pathDirection(timeOfDay); }
    /// Direction towards the moon.
    [[nodiscard]] Vec3 moonDirection() const noexcept
    {
        return pathDirection(timeOfDay + 12.0f + moonOffset);
    }
};

} // namespace illusion

/// Stable scene-file names of tonemappers.
template <>
struct illusion::EnumReflection<illusion::Tonemapper>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Aces", 0}, illusion::EnumValue{"AgX", 1}};
};

/// Stable scene-file names of camera projection modes.
template <>
struct illusion::EnumReflection<illusion::CameraProjection>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Perspective", 0},
                                        illusion::EnumValue{"Orthographic", 1}};
};
