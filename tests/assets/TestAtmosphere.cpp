#include <illusion/assets/Atmosphere.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace illusion;

namespace
{

AtmosphereDesc sunAt(f32 elevation)
{
    AtmosphereDesc desc;
    desc.sunDirection = Vec3(0.0f, std::sin(elevation), -std::cos(elevation));
    desc.moonDirection = -desc.sunDirection;
    return desc;
}

} // namespace

TEST_CASE("a high sun gives a blue sky, a brighter horizon and white sunlight")
{
    const AtmosphereDesc noon = sunAt(1.2f);
    const Vec3 zenith = atmosphereRadiance(noon, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(zenith.b > zenith.g);
    CHECK(zenith.g > zenith.r);
    CHECK(zenith.b > 0.1f);
    CHECK(zenith.b < 2.0f);
    const Vec3 horizon = atmosphereRadiance(noon, Vec3(1.0f, 0.02f, 0.0f));
    CHECK(horizon.r + horizon.g + horizon.b > zenith.r + zenith.g + zenith.b);
    const Vec3 sunlight = atmosphereTransmittance(noon, noon.sunDirection);
    CHECK(sunlight.r > 0.9f);
    CHECK(sunlight.b > 0.6f);
    // Towards the sun the haze glows (forward scattering).
    const Vec3 nearSun =
        atmosphereRadiance(noon, glm::normalize(noon.sunDirection + Vec3(0.05f, 0.0f, 0.0f)));
    CHECK(nearSun.r > zenith.r * 2.0f);
}

TEST_CASE("a low sun is orange and the sky goes dark after sunset")
{
    const AtmosphereDesc low = sunAt(0.03f);
    const Vec3 sunlight = atmosphereTransmittance(low, low.sunDirection);
    CHECK(sunlight.r > 2.0f * sunlight.b);
    const Vec3 glow = atmosphereRadiance(low, glm::normalize(Vec3(0.0f, 0.05f, -1.0f)));
    CHECK(glow.r > glow.b);

    const AtmosphereDesc night = sunAt(-0.4f);
    const Vec3 zenith = atmosphereRadiance(night, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(zenith.r + zenith.g + zenith.b < 0.01f);
    CHECK(zenith.b > 0.0f); // the night glow, bluish
    // A full moon brightens the night sky a little.
    AtmosphereDesc moonlit = night;
    moonlit.moonRadiance = 0.02f;
    const Vec3 moonSky = atmosphereRadiance(moonlit, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(moonSky.b > zenith.b);
}

TEST_CASE("the ground below the horizon blends into the horizon haze")
{
    const AtmosphereDesc noon = sunAt(1.0f);
    const Vec3 justBelow = atmosphereRadiance(noon, glm::normalize(Vec3(1.0f, -0.001f, 0.0f)));
    const Vec3 justAbove = atmosphereRadiance(noon, glm::normalize(Vec3(1.0f, 0.001f, 0.0f)));
    CHECK(justBelow.r == doctest::Approx(justAbove.r).epsilon(0.05));
    AtmosphereDesc red = noon;
    red.groundAlbedo = Vec3(0.8f, 0.0f, 0.0f);
    const Vec3 down = atmosphereRadiance(red, Vec3(0.0f, -1.0f, 0.0f));
    CHECK(down.r > 0.0f);
    CHECK(down.g == doctest::Approx(0.0f));
}

TEST_CASE("the sky's irradiance comes mostly from above by day and is tiny at night")
{
    const ShIrradiance day = atmosphereIrradiance(sunAt(1.0f));
    CHECK(day.evaluate(Vec3(0.0f, 1.0f, 0.0f)).b > day.evaluate(Vec3(0.0f, -1.0f, 0.0f)).b);
    CHECK(day.evaluate(Vec3(0.0f, 1.0f, 0.0f)).b > 0.1f);
    const ShIrradiance night = atmosphereIrradiance(sunAt(-0.5f));
    CHECK(night.evaluate(Vec3(0.0f, 1.0f, 0.0f)).b < 0.05f);
}

TEST_CASE("invalid inputs give finite radiance")
{
    AtmosphereDesc broken;
    broken.sunDirection = Vec3(0.0f);
    broken.moonDirection = Vec3(std::nanf(""));
    broken.turbidity = 1000.0f;
    const Vec3 radiance = atmosphereRadiance(broken, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(std::isfinite(radiance.r + radiance.g + radiance.b));
    CHECK(std::isfinite(glm::dot(atmosphereTransmittance(broken, Vec3(std::nanf(""))), Vec3(1.0f))));
}

TEST_CASE("ShAccumulator matches the irradiance of a uniform environment")
{
    ShAccumulator sums;
    constexpr u32 kWidth = 64;
    constexpr u32 kHeight = 32;
    const f64 area = (2.0 * std::numbers::pi / kWidth) * (std::numbers::pi / kHeight);
    for (u32 y = 0; y < kHeight; ++y)
    {
        const f32 v = (static_cast<f32>(y) + 0.5f) / kHeight;
        for (u32 x = 0; x < kWidth; ++x)
        {
            const Vec3 d = equirectDirection(Vec2((static_cast<f32>(x) + 0.5f) / kWidth, v));
            sums.add(d, Vec3(0.5f), area * std::sin(static_cast<f64>(v) * std::numbers::pi));
        }
    }
    const Vec3 expected = ShIrradiance::uniform(Vec3(0.5f)).evaluate(Vec3(0.0f, 0.0f, 1.0f));
    CHECK(sums.result().evaluate(Vec3(0.0f, 0.0f, 1.0f)).r == doctest::Approx(expected.r).epsilon(0.01));
}
