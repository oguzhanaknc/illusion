#pragma once

/// @file
/// Environment (sky) images in equirectangular layout: direction mapping, diffuse irradiance as
/// spherical harmonics, and a procedural sky.

#include <illusion/assets/Image.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>

namespace illusion
{

/// Direction for equirectangular texture coordinates @p uv (each in [0, 1], origin top-left).
///
/// The image centre (0.5, 0.5) faces -z (where an unrotated camera looks), u increases towards +x
/// (east), v = 0 is straight up (+y). This is the layout of Poly Haven and most HDRI libraries.
[[nodiscard]] Vec3 equirectDirection(Vec2 uv) noexcept;

/// Equirectangular texture coordinates of the unit direction @p direction (inverse of
/// equirectDirection()).
[[nodiscard]] Vec2 equirectUv(Vec3 direction) noexcept;

/// Diffuse irradiance of an environment, stored as 9 spherical-harmonics coefficients (bands 0-2) with
/// the cosine-lobe convolution already applied (Ramamoorthi and Hanrahan 2001).
struct ShIrradiance
{
    std::array<Vec3, 9>
        coefficients{}; ///< Coefficients in the order Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22.

    /// Irradiance arriving at a surface with unit normal @p normal. A Lambertian surface with albedo
    /// `a` reflects `a * evaluate(normal) / pi`.
    [[nodiscard]] Vec3 evaluate(Vec3 normal) const noexcept;

    /// Irradiance of a uniform environment of radiance @p radiance (pi * radiance from every direction).
    [[nodiscard]] static ShIrradiance uniform(Vec3 radiance) noexcept;
};

/// Projects the equirectangular image @p environment (mip 0, any format; 8-bit images are treated as
/// radiance in [0, 1]) onto spherical harmonics. Returns zero coefficients for an empty image.
[[nodiscard]] ShIrradiance computeShIrradiance(const Image& environment);

/// Parameters of makeSkyImage().
struct SkyDesc
{
    Vec3 zenith{0.25f, 0.45f, 0.9f};       ///< Linear radiance straight up.
    Vec3 horizon{0.9f, 0.95f, 1.0f};       ///< Linear radiance at the horizon.
    Vec3 ground{0.25f, 0.22f, 0.2f};       ///< Linear radiance below the horizon.
    Vec3 sunDirection{0.3f, 0.6f, -0.75f}; ///< Direction towards the sun (normalized internally).
    Vec3 sunRadiance{0.0f};                ///< Sun disc radiance; zero for no disc (use a DirectionalLight).
    f32 sunAngularRadius = 0.02f;          ///< Sun disc radius in radians.
};

/// Creates a simple procedural sky as a @p width x (@p width / 2) RGBA32F equirectangular image: a
/// zenith-to-horizon gradient, a ground colour and an optional sun disc. Useful as a default environment
/// and for tests.
[[nodiscard]] Image makeSkyImage(u32 width, const SkyDesc& desc = {});

} // namespace illusion
