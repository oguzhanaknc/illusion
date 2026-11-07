#pragma once

/// @file
/// The particle part of the scripting API: what a game module may ask of the host's ParticleWorld.
///
/// @code
/// if (illusion::ParticleService* particles = context.service<illusion::ParticleService>())
/// {
///     particles->emit(sparks, 40); // a burst where the emitter stands
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/particles/ParticleComponents.hpp>

namespace illusion
{

/// Particle effects of one Simulation World, implemented by the host (ParticleWorldService over a
/// ParticleWorld) and found by modules with `context.service<ParticleService>()`. The functions behave like
/// the ParticleWorld functions of the same name. Adding a function changes the module API version.
/// @ingroup scripting_api
class ParticleService : NonCopyable
{
public:
    /// Starts the emitter of @p entity again from the beginning; false without a ParticleEmitter.
    virtual bool play(Entity entity) = 0;
    /// Stops emitting; the live particles finish their lives. False without a ParticleEmitter.
    virtual bool stop(Entity entity) = 0;
    /// Removes the live particles at once; false if the emitter has not run yet.
    virtual bool clear(Entity entity) = 0;
    /// Emits up to @p count particles now, at the emitter, whether it plays or not. Returns how many fit.
    virtual u32 emit(Entity entity, u32 count) = 0;
    /// Live particles of the emitter of @p entity.
    [[nodiscard]] virtual u32 aliveCount(Entity entity) const = 0;
    /// Whether the emitter of @p entity emits.
    [[nodiscard]] virtual bool isPlaying(Entity entity) const = 0;
    /// Live particles of every emitter after the last update.
    [[nodiscard]] virtual u64 totalAlive() const = 0;

protected:
    ParticleService() = default;
    virtual ~ParticleService() = default;
};

} // namespace illusion
