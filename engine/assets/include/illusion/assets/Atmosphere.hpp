#pragma once

/// @file
/// An analytic sky for any sun and moon position: single scattering of air (Rayleigh) and haze (Mie) along
/// an air mass that grows towards the horizon. Cheap enough to evaluate per pixel and every frame; the
/// renderer's SkyAtmosphere uses the same model in `Atmosphere.slang` (ADR 0049).
///
/// ```cpp
/// illusion::AtmosphereDesc sky;
/// sky.sunDirection = illusion::Vec3(0.0f, 0.2f, -1.0f); // a low sun: orange light, a bright horizon
/// const illusion::Vec3 zenith = illusion::atmosphereRadiance(sky, illusion::Vec3(0.0f, 1.0f, 0.0f));
/// const illusion::Vec3 sunlight = illusion::atmosphereTransmittance(sky, sky.sunDirection);
/// ```

#include <illusion/assets/Environment.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// Inputs of the atmosphere model.
struct AtmosphereDesc
{
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};   ///< Towards the sun (normalized internally).
    Vec3 moonDirection{0.0f, -1.0f, 0.0f}; ///< Towards the moon (normalized internally).
    f32 turbidity = 2.0f;                  ///< Haze: 1 clear mountain air, 10 thick haze.
    f32 sunRadiance = 1.0f;                ///< Scale of the sunlight scattered into the sky.
    f32 moonRadiance = 0.0f;               ///< Scale of the moonlight scattered into the sky.
    Vec3 groundAlbedo{0.25f};              ///< Colour of the ground seen below the horizon.

    /// Compares every field.
    friend bool operator==(const AtmosphereDesc&, const AtmosphereDesc&) = default;
};

/// Fraction of light that reaches the ground from @p direction through the air, per colour channel: white
/// at the zenith, orange to red towards the horizon. Directions below the horizon use the horizon's.
[[nodiscard]] Vec3 atmosphereTransmittance(const AtmosphereDesc& desc, Vec3 direction) noexcept;

/// Radiance of the sky seen in the unit direction @p direction, without the sun and moon discs and the
/// stars: scattered sunlight and moonlight plus a faint night glow; the ground below the horizon.
[[nodiscard]] Vec3 atmosphereRadiance(const AtmosphereDesc& desc, Vec3 direction) noexcept;

/// Diffuse irradiance of the sky of @p desc as spherical harmonics, from @p width x (@p width / 2)
/// directions (an equirectangular grid). Used for image-based lighting of a changing sky.
[[nodiscard]] ShIrradiance atmosphereIrradiance(const AtmosphereDesc& desc, u32 width = 32) noexcept;

} // namespace illusion
