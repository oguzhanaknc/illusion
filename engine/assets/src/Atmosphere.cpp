#include <illusion/assets/Atmosphere.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

// Keep in step with engine/render/shaders/Atmosphere.slang: the renderer draws the same sky.

namespace illusion
{
namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;
// Optical depth of air straight up (Rayleigh, per colour) and of haze per unit of turbidity (Mie).
constexpr Vec3 kRayleighDepth{0.0464f, 0.108f, 0.265f};
constexpr f32 kMieDepthPerTurbidity = 0.005f;
// Ozone absorbs orange light without scattering it: twilight skies stay blue.
constexpr Vec3 kOzoneDepth{0.03f, 0.08f, 0.004f};
constexpr f32 kMieAsymmetry = 0.76f;
// Radiance of the sky per unit of AtmosphereDesc::sunRadiance (a noon sky about 0.1 to 0.5).
constexpr f32 kSkyScale = 30.0f;
// The moon's light is bluish to the eye; the night sky keeps a faint glow (airglow, light pollution).
constexpr Vec3 kMoonTint{0.55f, 0.7f, 1.0f};
constexpr Vec3 kNightGlow{0.0006f, 0.0009f, 0.0018f};

Vec3 safeNormalize(Vec3 v, Vec3 fallback) noexcept
{
    const f32 length = glm::length(v);
    return length > 1e-6f && std::isfinite(length) ? v / length : fallback;
}

// Relative air mass towards elevation sine @p up (Kasten and Young 1989): 1 at the zenith, about 38 at
// the horizon.
f32 airMass(f32 up) noexcept
{
    const f32 cosine = std::clamp(up, 0.0f, 1.0f);
    const f32 degrees = std::acos(cosine) * (180.0f / kPi);
    return 1.0f / (cosine + 0.15f * std::pow(std::max(93.885f - degrees, 1e-3f), -1.253f));
}

Vec3 mieDepth(const AtmosphereDesc& desc) noexcept
{
    return Vec3(kMieDepthPerTurbidity * std::clamp(desc.turbidity, 1.0f, 10.0f));
}

// Light from a source in direction @p light scattered towards the eye along @p view, per unit of source
// strength. The source fades out through twilight as it sinks below the horizon.
Vec3 scattered(const AtmosphereDesc& desc, Vec3 view, Vec3 light) noexcept
{
    const f32 twilight = glm::smoothstep(-0.15f, 0.02f, light.y);
    if (twilight <= 0.0f)
    {
        return Vec3(0.0f);
    }
    const f32 cosine = glm::dot(view, light);
    const f32 rayleighPhase = 3.0f / (16.0f * kPi) * (1.0f + cosine * cosine);
    const f32 g = kMieAsymmetry;
    const f32 miePhase =
        (1.0f - g * g) / (4.0f * kPi * std::pow(std::max(1.0f + g * g - 2.0f * g * cosine, 1e-4f), 1.5f));
    const Vec3 mie = mieDepth(desc);
    const Vec3 extinction = kRayleighDepth + mie + kOzoneDepth;
    const Vec3 lightThrough = glm::exp(-extinction * airMass(light.y));
    const Vec3 viewDepth = extinction * airMass(view.y);
    const Vec3 albedo = (kRayleighDepth * rayleighPhase + mie * miePhase) / extinction;
    return twilight * lightThrough * albedo * (Vec3(1.0f) - glm::exp(-viewDepth));
}

// The sky above the horizon (or at it, for directions below).
Vec3 skyRadiance(const AtmosphereDesc& desc, Vec3 view, Vec3 sun, Vec3 moon) noexcept
{
    const f32 sunScale = kSkyScale * std::max(desc.sunRadiance, 0.0f);
    const f32 moonScale = kSkyScale * std::max(desc.moonRadiance, 0.0f);
    return scattered(desc, view, sun) * sunScale + scattered(desc, view, moon) * (moonScale * kMoonTint) +
           kNightGlow * std::max(desc.sunRadiance, 0.0f);
}

} // namespace

Vec3 atmosphereTransmittance(const AtmosphereDesc& desc, Vec3 direction) noexcept
{
    const Vec3 d = safeNormalize(direction, Vec3(0.0f, 1.0f, 0.0f));
    return glm::exp(-(kRayleighDepth + mieDepth(desc) + kOzoneDepth) * airMass(d.y));
}

Vec3 atmosphereRadiance(const AtmosphereDesc& desc, Vec3 direction) noexcept
{
    const Vec3 view = safeNormalize(direction, Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 sun = safeNormalize(desc.sunDirection, Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 moon = safeNormalize(desc.moonDirection, Vec3(0.0f, -1.0f, 0.0f));
    if (view.y >= 0.0f)
    {
        return skyRadiance(desc, view, sun, moon);
    }
    // Below the horizon: the ground, lit by the sun and the sky, fading into the horizon haze.
    const Vec3 horizon =
        skyRadiance(desc, safeNormalize(Vec3(view.x, 0.0f, view.z), Vec3(1.0f, 0.0f, 0.0f)), sun, moon);
    const Vec3 zenith = skyRadiance(desc, Vec3(0.0f, 1.0f, 0.0f), sun, moon);
    const Vec3 sunlight =
        atmosphereTransmittance(desc, sun) * (glm::smoothstep(-0.05f, 0.1f, sun.y) * std::max(sun.y, 0.0f) *
                                              kSkyScale * 0.05f * std::max(desc.sunRadiance, 0.0f));
    const Vec3 ground = glm::max(desc.groundAlbedo, Vec3(0.0f)) * (sunlight + zenith);
    return glm::mix(ground, horizon, std::exp(view.y * 12.0f));
}

ShIrradiance atmosphereIrradiance(const AtmosphereDesc& desc, u32 width) noexcept
{
    width = std::max(width, 4u);
    const u32 height = width / 2;
    ShAccumulator sums;
    const f64 texelArea = (2.0 * std::numbers::pi / width) * (std::numbers::pi / height);
    for (u32 y = 0; y < height; ++y)
    {
        const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height);
        const f64 weight = texelArea * std::sin(static_cast<f64>(v) * std::numbers::pi);
        for (u32 x = 0; x < width; ++x)
        {
            const Vec3 direction =
                equirectDirection(Vec2((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width), v));
            sums.add(direction, atmosphereRadiance(desc, direction), weight);
        }
    }
    return sums.result();
}

} // namespace illusion
