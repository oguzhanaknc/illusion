#pragma once

/// @file
/// What a SkyAtmosphere looks like at its time of day: the atmosphere model's inputs, its main light (the sun
/// by day, the moon by night) and how visible the stars are. The renderer draws from this; game code can
/// use it to match lighting, for example to switch lamps on at dusk.
///
/// ```cpp
/// const illusion::SkyAtmosphereState sky = illusion::evaluateSkyAtmosphere(atmosphere);
/// const bool night = sky.starVisibility > 0.5f;
/// ```

#include <illusion/assets/Atmosphere.hpp>
#include <illusion/render/SceneComponents.hpp>

namespace illusion
{

/// The directional light of a SkyAtmosphere.
struct SkyAtmosphereLight
{
    Vec3 direction{0.0f, -1.0f, 0.0f}; ///< Direction the light travels (from the sun or moon).
    Vec3 color{1.0f};                  ///< Linear colour after the air (orange near the horizon).
    f32 intensity = 0.0f;              ///< Illuminance multiplier; 0 when neither body is up.
    bool moon = false;                 ///< The light is the moon's.
};

/// A SkyAtmosphere evaluated at its time of day.
struct SkyAtmosphereState
{
    AtmosphereDesc atmosphere;   ///< Inputs of the sky model.
    SkyAtmosphereLight light;    ///< The main light.
    f32 moonIllumination = 1.0f; ///< Lit fraction of the moon's disc: 1 full, 0 new.
    f32 starVisibility = 0.0f;   ///< 0 by day to 1 at night.
};

/// Evaluates @p sky: the sun lights the scene until it sets, then the moon, fading so that the light does
/// not jump when it changes body.
[[nodiscard]] SkyAtmosphereState evaluateSkyAtmosphere(const SkyAtmosphere& sky) noexcept;

} // namespace illusion
