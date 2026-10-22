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

/// Perspective camera (reflected as "illusion.Camera"). The renderer looks through the Transform's -z axis.
struct Camera
{
    f32 fovY = 1.0471976f; ///< Vertical field of view in radians (60 degrees).
    f32 nearPlane = 0.1f;  ///< Near clip distance. There is no far plane (reversed-Z, infinite).
    f32 exposure = 1.0f;   ///< Linear exposure multiplier applied before tonemapping.
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

} // namespace illusion
