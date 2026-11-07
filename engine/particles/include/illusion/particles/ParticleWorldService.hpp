#pragma once

/// @file
/// The host's implementation of ParticleService over ParticleWorld.
///
/// @code
/// illusion::ParticleWorld particles(world);
/// illusion::ParticleWorldService particleService(particles);
/// services.provide<illusion::ParticleService>(particleService);
/// @endcode

#include <illusion/particles/ParticleService.hpp>
#include <illusion/particles/ParticleWorld.hpp>

namespace illusion
{

/// ParticleService that forwards every call to a ParticleWorld, which must outlive it. Created by the host;
/// a shared module that constructs one fails to link, since only the host has the library's code (ADR 0041).
class ParticleWorldService final : public ParticleService
{
public:
    /// Serves @p particles.
    explicit ParticleWorldService(ParticleWorld& particles) noexcept
        : m_particles(particles)
    {
    }
    ~ParticleWorldService() override;

    /// The ParticleWorld it serves.
    [[nodiscard]] ParticleWorld& particles() const noexcept { return m_particles; }

    /// @name ParticleService
    /// @{
    bool play(Entity entity) override;
    bool stop(Entity entity) override;
    bool clear(Entity entity) override;
    u32 emit(Entity entity, u32 count) override;
    [[nodiscard]] u32 aliveCount(Entity entity) const override;
    [[nodiscard]] bool isPlaying(Entity entity) const override;
    [[nodiscard]] u64 totalAlive() const override;
    /// @}

private:
    ParticleWorld& m_particles;
};

} // namespace illusion
