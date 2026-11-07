#include <illusion/render/SkyAtmosphere.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace illusion;

TEST_CASE("the sun rises at +x at six, stands high at noon and sets at -x at eighteen")
{
    SkyAtmosphere sky;
    sky.sunTilt = 0.5f;
    sky.timeOfDay = 6.0f;
    CHECK(sky.sunDirection().x == doctest::Approx(1.0));
    CHECK(sky.sunDirection().y == doctest::Approx(0.0).epsilon(1e-5));
    sky.timeOfDay = 12.0f;
    CHECK(sky.sunDirection().y == doctest::Approx(std::cos(0.5f)));
    CHECK(sky.sunDirection().z == doctest::Approx(-std::sin(0.5f)));
    sky.timeOfDay = 18.0f;
    CHECK(sky.sunDirection().x == doctest::Approx(-1.0));
    sky.timeOfDay = 0.0f;
    CHECK(sky.sunDirection().y < -0.8f);
    // A full moon stands opposite the sun; the azimuth turns the whole path.
    CHECK(glm::dot(sky.sunDirection(), sky.moonDirection()) == doctest::Approx(-1.0));
    sky.timeOfDay = 6.0f;
    sky.sunAzimuth = std::numbers::pi_v<f32> / 2.0f;
    CHECK(sky.sunDirection().z == doctest::Approx(-1.0));
}

TEST_CASE("the sun lights the day, the moon the night, and the light fades out before it changes body")
{
    SkyAtmosphere sky;
    sky.timeOfDay = 12.0f;
    SkyAtmosphereState state = evaluateSkyAtmosphere(sky);
    CHECK_FALSE(state.light.moon);
    CHECK(state.light.intensity == doctest::Approx(sky.sunIntensity));
    CHECK(state.light.direction.y < -0.5f); // shining down
    CHECK(state.starVisibility == doctest::Approx(0.0));
    CHECK(state.moonIllumination == doctest::Approx(1.0));

    sky.timeOfDay = 17.9f; // a low sun is orange and dimmer
    state = evaluateSkyAtmosphere(sky);
    CHECK_FALSE(state.light.moon);
    CHECK(state.light.color.r > 2.0f * state.light.color.b);

    sky.timeOfDay = 0.0f;
    state = evaluateSkyAtmosphere(sky);
    CHECK(state.light.moon);
    CHECK(state.light.intensity > 0.5f * sky.moonIntensity);
    CHECK(state.light.color.b > state.light.color.r);
    CHECK(state.starVisibility == doctest::Approx(1.0));

    // Around sunset the light never jumps: it goes dark before switching to the moon.
    f32 previous = evaluateSkyAtmosphere(SkyAtmosphere{.timeOfDay = 17.5f}).light.intensity;
    for (f32 hours = 17.5f; hours < 20.0f; hours += 0.01f)
    {
        const f32 intensity = evaluateSkyAtmosphere(SkyAtmosphere{.timeOfDay = hours}).light.intensity;
        CHECK(std::abs(intensity - previous) < 0.2f);
        previous = intensity;
    }
}

TEST_CASE("a new moon gives no moonlight and invalid values give no light")
{
    SkyAtmosphere sky;
    sky.timeOfDay = 0.0f;
    sky.moonOffset = 12.0f; // the moon stands with the sun
    const SkyAtmosphereState state = evaluateSkyAtmosphere(sky);
    CHECK(state.moonIllumination == doctest::Approx(0.0).epsilon(1e-4));
    CHECK(state.light.intensity == doctest::Approx(0.0));

    SkyAtmosphere broken;
    broken.sunTilt = std::nanf("");
    const SkyAtmosphereState none = evaluateSkyAtmosphere(broken);
    CHECK(none.light.intensity == 0.0f);
    broken = SkyAtmosphere{};
    broken.timeOfDay = std::nanf("");
    CHECK(std::isfinite(evaluateSkyAtmosphere(broken).light.intensity));
}
