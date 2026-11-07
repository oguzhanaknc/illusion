#pragma once

/// @file
/// The Wind Component: the wind of a scene. It bends and flutters vegetation (materials with
/// MaterialData::windSway or windFlutter) and pushes particles (ParticleEmitter::windFactor).
///
/// @code
/// world.create(illusion::Name{"Wind"}, illusion::Wind{.direction = {1.0f, 0.0f, 0.3f}, .speed = 4.0f});
/// // each frame, before rendering and particles:
/// illusion::advanceWind(world, deltaSeconds);
/// const illusion::Vec3 push = illusion::windVelocity(wind, state.time, position);
/// @endcode
///
/// A scene has one wind; with several, the renderer and particles use the first active one.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

class World;

/// Wind of a scene (reflected as "illusion.Wind"): a steady direction and speed with gusts that travel
/// downwind. See windVelocity() for the model.
struct Wind
{
    Vec3 direction{1.0f, 0.0f, 0.0f}; ///< Direction the wind blows toward; normalized when used.
    f32 speed = 3.0f;                 ///< Average speed in metres per second.
    f32 gustStrength = 0.4f;          ///< How much gusts change the speed: 0 steady, 1 from calm to double.
    f32 gustFrequency = 0.15f;        ///< Gusts per second at a point, in hertz.
    f32 turbulence = 1.0f;            ///< Multiplies the flutter of leaves and grass.

    /// Compares every field.
    friend bool operator==(const Wind&, const Wind&) = default;
};

/// Clock of a Wind (runtime state, reflected as "illusion.WindState"), advanced by advanceWind().
struct WindState
{
    f32 time = 0.0f;         ///< Seconds the wind has blown.
    f32 previousTime = 0.0f; ///< time before the last advanceWind(), for motion vectors.
};

/// Advances the clock of every Wind by @p seconds, adding the WindState of new ones. Call once per frame
/// in the World that is rendered (GameSession and the editor's preview do).
void advanceWind(World& world, f32 seconds);

/// Gust factor of @p wind at @p position after @p time seconds: 1 on average, between 1 - gustStrength and
/// 1 + gustStrength. Gust fronts travel downwind at the wind's speed, so nearby plants bend one after the
/// other. Matches windGust() in the scene shaders.
[[nodiscard]] f32 windGust(const Wind& wind, f32 time, Vec3 position) noexcept;

/// Velocity of the air at @p position after @p time seconds, in metres per second: the normalized direction
/// times speed times windGust(). Zero for a zero direction.
[[nodiscard]] Vec3 windVelocity(const Wind& wind, f32 time, Vec3 position) noexcept;

} // namespace illusion
