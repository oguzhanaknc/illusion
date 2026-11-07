#pragma once

/// @file
/// ParticleWorld: simulates the ParticleEmitter Components of a World (ADR 0047).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <vector>

namespace illusion
{

class JobSystem;
class World;

/// Settings of a ParticleWorld.
struct ParticleWorldDesc
{
    Vec3 gravity{0.0f, -9.81f, 0.0f}; ///< Acceleration in m/s² that ParticleEmitter::gravityFactor scales.
};

/// Emits, moves and retires the particles of every ParticleEmitter in a World, keeping them in the
/// emitters' ParticlePool Components. Deterministic: the same emitters, seeds and time steps give the same
/// particles, whatever the number of threads. Allocates only when a pool is created or its capacity
/// changes. Emitters on Inactive entities pause.
class ParticleWorld : NonCopyable
{
public:
    /// Simulates the emitters of @p world, which must outlive this object.
    explicit ParticleWorld(World& world, const ParticleWorldDesc& desc = {});

    /// Advances every emitter by @p deltaSeconds (negative values count as 0). Emitters run as parallel
    /// Jobs on @p jobs when given. Adds a ParticlePool to emitters that have none, so call it outside
    /// queries of the World.
    void update(f32 deltaSeconds, JobSystem* jobs = nullptr);

    /// Starts @p entity's emitter again from the beginning: its particles are cleared, the burst fires on
    /// the next update and a non-looping emitter emits for its duration again. Returns false if the entity
    /// has no ParticleEmitter.
    bool play(Entity entity);

    /// Stops emitting from @p entity's emitter; its live particles finish their lives. Returns false if the
    /// entity has no ParticleEmitter.
    bool stop(Entity entity);

    /// Removes the live particles of @p entity's emitter at once. Returns false if it has no pool yet.
    bool clear(Entity entity);

    /// Live particles of @p entity's emitter (0 without a pool).
    [[nodiscard]] u32 aliveCount(Entity entity) const;

    /// Whether @p entity's emitter emits, or will on its next update.
    [[nodiscard]] bool isPlaying(Entity entity) const;

    /// Live particles of every emitter, counted by the last update().
    [[nodiscard]] u64 totalAlive() const noexcept { return m_totalAlive; }

    /// Changes the gravity.
    void setGravity(const Vec3& gravity) noexcept { m_desc.gravity = gravity; }

    /// The gravity.
    [[nodiscard]] const Vec3& gravity() const noexcept { return m_desc.gravity; }

private:
    World& m_world;
    ParticleWorldDesc m_desc;
    u64 m_totalAlive = 0;
    std::vector<Entity> m_missing; // emitters without a pool, gathered by update()
};

} // namespace illusion
