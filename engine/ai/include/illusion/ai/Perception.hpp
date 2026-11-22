#pragma once

/// @file
/// The geometry of perception (docs/adr/0062-navigation-and-ai.md): pure tests of a point against a sight
/// cone and of a noise against a listener, used by AiWorld and tested without a World.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <cmath>
#include <numbers>

namespace illusion
{

/// Most Entities one AiPerception remembers.
inline constexpr u32 kMaxPerceived = 8;

/// How something was perceived.
enum class AiSense : u8
{
    Sight,   ///< Seen in the cone with a clear line of sight.
    Hearing, ///< A noise it made was heard.
};

/// A noise reported with AiService::reportNoise(), heard for one update.
struct AiNoise
{
    Vec3 position{0.0f}; ///< Where it was made, in world space.
    f32 radius = 10.0f;  ///< Distance in m at which a listener with hearing 1 hears it.
    Entity instigator;   ///< Who made it (perceived as its source), or null.
    u32 team = 0;        ///< Team of the noise, in [0, kMaxAiTeams).
};

/// Something a perceiver senses or remembers.
struct AiPerceived
{
    Entity entity;                  ///< What was perceived (a noise's instigator).
    AiSense sense = AiSense::Sight; ///< The sense of the latest perception.
    Vec3 position{0.0f};            ///< Last known position in world space.
    f32 age = 0.0f;                 ///< Seconds since it was last sensed.
    bool current = false;           ///< Sensed in the last update.
};

/// True when @p point lies within @p radius of @p eye and within the cone of full angle @p angle (radians;
/// 2 pi or more sees all around) around the unit @p forward. Both bounds are inclusive and the eye itself is
/// inside. Does not test the line of sight.
[[nodiscard]] inline bool inSightCone(const Vec3& eye, const Vec3& forward, f32 radius, f32 angle,
                                      const Vec3& point) noexcept
{
    const Vec3 offset = point - eye;
    const f32 distanceSquared = glm::dot(offset, offset);
    if (!(radius > 0.0f) || distanceSquared > radius * radius)
    {
        return false;
    }
    if (distanceSquared == 0.0f || angle >= 2.0f * std::numbers::pi_v<f32>)
    {
        return true;
    }
    if (!(angle > 0.0f))
    {
        return false;
    }
    return glm::dot(offset, forward) >= std::cos(angle * 0.5f) * std::sqrt(distanceSquared);
}

/// True when a listener at @p listener with @p hearing hears @p noise: within the noise's radius times
/// @p hearing (inclusive). A listener with hearing 0 or a noise of radius 0 hears nothing.
[[nodiscard]] inline bool hearsNoise(const Vec3& listener, f32 hearing, const AiNoise& noise) noexcept
{
    const f32 reach = noise.radius * hearing;
    if (!(hearing > 0.0f) || !(reach > 0.0f))
    {
        return false;
    }
    const Vec3 offset = noise.position - listener;
    return glm::dot(offset, offset) <= reach * reach;
}

} // namespace illusion
