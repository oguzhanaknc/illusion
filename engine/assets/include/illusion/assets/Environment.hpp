#pragma once

/// @file
/// Environment (sky) images in equirectangular layout: direction mapping, diffuse irradiance as
/// spherical harmonics, a procedural sky, and baked image-based lighting as stored in the import cache.

#include <illusion/assets/Image.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <vector>

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

/// Number of texels of a cube map with face size @p size and @p mips mip levels (all six faces).
[[nodiscard]] constexpr usize cubeTexelCount(u32 size, u32 mips) noexcept
{
    usize count = 0;
    for (u32 mip = 0; mip < mips; ++mip)
    {
        const usize face = size >> mip > 0 ? size >> mip : 1;
        count += face * face * 6;
    }
    return count;
}

/// Image-based lighting baked from an environment image by RenderResources (a background cube map with a
/// full mip chain and a specular cube map prefiltered for increasing roughness), with its diffuse
/// irradiance. RenderResources reads it back after a bake and creates environments from it without baking;
/// the renderer keeps it in the import cache (AssetType::Environment).
///
/// Texels are RGBA16F (glm::packHalf4x16) in mip order, then face order (+x, -x, +y, -y, +z, -z), then rows
/// from the top; a face of mip `m` is `max(size >> m, 1)` texels wide.
struct BakedEnvironment
{
    u32 backgroundSize = 0; ///< Face size of the background cube map (a power of two).
    u32 backgroundMips = 0; ///< Mip levels of the background cube map.
    u32 specularSize = 0;   ///< Face size of the specular cube map (a power of two).
    u32 specularMips = 0;   ///< Mip levels of the specular cube map (mip i is for roughness i / (mips - 1)).
    std::vector<u64> background; ///< cubeTexelCount(backgroundSize, backgroundMips) texels.
    std::vector<u64> specular;   ///< cubeTexelCount(specularSize, specularMips) texels.
    ShIrradiance irradiance;     ///< Diffuse irradiance.

    /// True if both sizes are non-zero powers of two, the mip counts fit them and the texel counts match.
    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace illusion
