#include <illusion/render/SkyAtmosphere.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{

SkyAtmosphereState evaluateSkyAtmosphere(const SkyAtmosphere& sky) noexcept
{
    SkyAtmosphere valid = sky;
    if (!std::isfinite(valid.timeOfDay))
    {
        valid.timeOfDay = 12.0f;
    }
    const Vec3 sun = valid.sunDirection();
    const Vec3 moon = valid.moonDirection();

    SkyAtmosphereState state;
    state.atmosphere.sunDirection = sun;
    state.atmosphere.moonDirection = moon;
    state.atmosphere.turbidity = std::clamp(std::isfinite(sky.turbidity) ? sky.turbidity : 2.5f, 1.0f, 10.0f);
    const f32 skyIntensity = std::isfinite(sky.skyIntensity) ? std::max(sky.skyIntensity, 0.0f) : 1.0f;
    state.moonIllumination = std::clamp(0.5f * (1.0f - glm::dot(sun, moon)), 0.0f, 1.0f);
    state.atmosphere.sunRadiance = skyIntensity;
    // The moon reflects a few percent of the light of the sun and scatters it into a faint blue night sky.
    state.atmosphere.moonRadiance = skyIntensity * 0.02f * state.moonIllumination;
    state.atmosphere.groundAlbedo = glm::clamp(sky.groundAlbedo, Vec3(0.0f), Vec3(1.0f));
    state.starVisibility = 1.0f - glm::smoothstep(-0.18f, 0.0f, sun.y);

    // The sun fades out just below the horizon; the moon fades in only once the sun is well below, so
    // the light is dark when it changes body.
    const f32 sunFactor = glm::smoothstep(-0.03f, 0.06f, sun.y);
    const f32 night = 1.0f - glm::smoothstep(-0.12f, -0.03f, sun.y);
    const f32 moonFactor = night * glm::smoothstep(-0.03f, 0.06f, moon.y);
    if (sunFactor > 0.0f)
    {
        state.light.direction = -sun;
        state.light.color =
            atmosphereTransmittance(state.atmosphere, Vec3(sun.x, std::max(sun.y, 0.02f), sun.z));
        state.light.intensity = std::max(sky.sunIntensity, 0.0f) * sunFactor;
    }
    else
    {
        state.light.direction = -moon;
        state.light.color =
            Vec3(0.6f, 0.7f, 1.0f) *
            atmosphereTransmittance(state.atmosphere, Vec3(moon.x, std::max(moon.y, 0.02f), moon.z));
        state.light.intensity = std::max(sky.moonIntensity, 0.0f) * moonFactor * state.moonIllumination;
        state.light.moon = true;
    }
    const bool finite = std::isfinite(state.light.intensity) &&
                        std::isfinite(glm::dot(state.light.direction, state.light.color));
    if (!finite) // a non-finite angle in the Component
    {
        state.light = SkyAtmosphereLight{};
    }
    return state;
}

} // namespace illusion
