#include <illusion/assets/Environment.hpp>
#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace illusion
{
namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;

// Real spherical-harmonics basis, bands 0-2.
std::array<f32, 9> shBasis(Vec3 d) noexcept
{
    return {0.282095f,
            0.488603f * d.y,
            0.488603f * d.z,
            0.488603f * d.x,
            1.092548f * d.x * d.y,
            1.092548f * d.y * d.z,
            0.315392f * (3.0f * d.z * d.z - 1.0f),
            1.092548f * d.x * d.z,
            0.546274f * (d.x * d.x - d.y * d.y)};
}

// Cosine-lobe convolution factors per band (A0 = pi, A1 = 2pi/3, A2 = pi/4).
constexpr std::array<f32, 9> kBandFactors = {
    kPi,        2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, kPi / 4.0f,
    kPi / 4.0f, kPi / 4.0f,        kPi / 4.0f,        kPi / 4.0f};

} // namespace

Vec3 equirectDirection(Vec2 uv) noexcept
{
    const f32 phi = (uv.x - 0.5f) * 2.0f * kPi; // 0 at -z, +pi/2 at +x
    const f32 theta = uv.y * kPi;               // 0 at +y
    const f32 sinTheta = std::sin(theta);
    return Vec3(sinTheta * std::sin(phi), std::cos(theta), -sinTheta * std::cos(phi));
}

Vec2 equirectUv(Vec3 direction) noexcept
{
    const f32 phi = std::atan2(direction.x, -direction.z);
    const f32 theta = std::acos(std::clamp(direction.y, -1.0f, 1.0f));
    return Vec2(phi / (2.0f * kPi) + 0.5f, theta / kPi);
}

Vec3 ShIrradiance::evaluate(Vec3 normal) const noexcept
{
    const std::array<f32, 9> basis = shBasis(normal);
    Vec3 result(0.0f);
    for (usize i = 0; i < basis.size(); ++i)
    {
        result += coefficients[i] * basis[i];
    }
    return glm::max(result, Vec3(0.0f));
}

ShIrradiance ShIrradiance::uniform(Vec3 radiance) noexcept
{
    // L00 of a constant function is radiance * sqrt(4 pi); convolved and evaluated: pi * radiance.
    ShIrradiance sh;
    sh.coefficients[0] = radiance * (kPi / 0.282095f);
    return sh;
}

ShIrradiance computeShIrradiance(const Image& environment)
{
    ILL_PROFILE_SCOPE();
    ShIrradiance sh;
    if (!environment.isValid())
    {
        return sh;
    }
    const u32 width = environment.width();
    const u32 height = environment.height();
    std::array<glm::dvec3, 9> sums{};
    // Solid angle of a texel: (2 pi / width) * (pi / height) * sin(theta).
    const f64 texelArea = (2.0 * std::numbers::pi / width) * (std::numbers::pi / height);
    for (u32 y = 0; y < height; ++y)
    {
        const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height);
        const f64 weight = texelArea * std::sin(static_cast<f64>(v) * std::numbers::pi);
        for (u32 x = 0; x < width; ++x)
        {
            const Vec3 direction =
                equirectDirection(Vec2((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width), v));
            const glm::dvec3 radiance = glm::dvec3(Vec3(readPixel(environment, x, y))) * weight;
            const std::array<f32, 9> basis = shBasis(direction);
            for (usize i = 0; i < basis.size(); ++i)
            {
                sums[i] += radiance * static_cast<f64>(basis[i]);
            }
        }
    }
    for (usize i = 0; i < sums.size(); ++i)
    {
        sh.coefficients[i] = Vec3(sums[i]) * kBandFactors[i];
    }
    return sh;
}

Image makeSkyImage(u32 width, const SkyDesc& desc)
{
    width = std::max(width, 2u);
    const u32 height = width / 2;
    Image image = makeSolidImage(width, height, Vec4(0.0f), ImageFormat::RGBA32F);
    auto* pixels = reinterpret_cast<f32*>(image.data.data());
    const Vec3 sun = glm::normalize(desc.sunDirection);
    const f32 sunCos = std::cos(desc.sunAngularRadius);
    const bool hasSun = glm::dot(desc.sunRadiance, desc.sunRadiance) > 0.0f;
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const Vec3 d = equirectDirection(Vec2((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width),
                                                  (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height)));
            Vec3 radiance;
            if (d.y >= 0.0f)
            {
                // Most of the gradient happens near the horizon.
                radiance = glm::mix(desc.horizon, desc.zenith, std::pow(d.y, 0.5f));
            }
            else
            {
                radiance = glm::mix(desc.horizon, desc.ground, std::min(-d.y * 8.0f, 1.0f));
            }
            if (hasSun && glm::dot(d, sun) >= sunCos)
            {
                radiance = desc.sunRadiance;
            }
            f32* p = pixels + (static_cast<usize>(y) * width + x) * 4;
            p[0] = radiance.r;
            p[1] = radiance.g;
            p[2] = radiance.b;
            p[3] = 1.0f;
        }
    }
    return image;
}

} // namespace illusion
