#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace illusion
{

namespace
{

Vec3 normalizedDirection(const Wind& wind) noexcept
{
    const f32 length = glm::length(wind.direction);
    return length > 1e-6f && std::isfinite(length) ? wind.direction / length : Vec3(0.0f);
}

} // namespace

void advanceWind(World& world, f32 seconds)
{
    ILL_PROFILE_SCOPE();
    const f32 step = std::isfinite(seconds) ? std::max(seconds, 0.0f) : 0.0f;
    // New winds are rare (scene load) and few; each gets its clock outside the query, without a list.
    for (;;)
    {
        std::optional<Entity> missing;
        world.query<const Wind>().without<WindState>().each([&missing](Entity entity, const Wind&) {
            if (!missing)
            {
                missing = entity;
            }
        });
        if (!missing)
        {
            break;
        }
        world.emplace<WindState>(*missing);
    }
    world.query<WindState>().each([step](WindState& state) {
        state.previousTime = state.time;
        state.time += step;
    });
}

f32 windGust(const Wind& wind, f32 time, Vec3 position) noexcept
{
    const Vec3 direction = normalizedDirection(wind);
    const f32 speed = std::max(wind.speed, 0.1f);
    // Time at which the gust now at @p position passed the origin.
    const f32 phase = 2.0f * std::numbers::pi_v<f32> * std::max(wind.gustFrequency, 0.0f) *
                      (time - glm::dot(position, direction) / speed);
    const f32 wave = 0.6f * std::sin(phase) + 0.4f * std::sin(2.7f * phase + 1.3f);
    return std::max(1.0f + std::clamp(wind.gustStrength, 0.0f, 1.0f) * wave, 0.0f);
}

Vec3 windVelocity(const Wind& wind, f32 time, Vec3 position) noexcept
{
    return normalizedDirection(wind) * (std::max(wind.speed, 0.0f) * windGust(wind, time, position));
}

} // namespace illusion
