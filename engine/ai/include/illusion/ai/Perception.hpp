#pragma once

/// @file
/// The geometry of perception (docs/adr/0062-navigation-and-ai.md): pure tests of a point against a sight
/// cone and of a noise against a listener, used by AiWorld and tested without a World.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

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

/// True when @p point lies within @p radius of @p eye and within the cone of full angle @p angle around the
/// unit @p forward. Does not test the line of sight.
[[nodiscard]] bool inSightCone(const Vec3& eye, const Vec3& forward, f32 radius, f32 angle,
                               const Vec3& point) noexcept;

/// True when a listener at @p listener with @p hearing hears @p noise.
[[nodiscard]] bool hearsNoise(const Vec3& listener, f32 hearing, const AiNoise& noise) noexcept;

} // namespace illusion
