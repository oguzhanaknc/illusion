#include <illusion/particles/ParticleWorldService.hpp>

namespace illusion
{

ParticleWorldService::~ParticleWorldService() = default;

bool ParticleWorldService::play(Entity entity)
{
    return m_particles.play(entity);
}

bool ParticleWorldService::stop(Entity entity)
{
    return m_particles.stop(entity);
}

bool ParticleWorldService::clear(Entity entity)
{
    return m_particles.clear(entity);
}

u32 ParticleWorldService::emit(Entity entity, u32 count)
{
    return m_particles.emit(entity, count);
}

u32 ParticleWorldService::aliveCount(Entity entity) const
{
    return m_particles.aliveCount(entity);
}

bool ParticleWorldService::isPlaying(Entity entity) const
{
    return m_particles.isPlaying(entity);
}

u64 ParticleWorldService::totalAlive() const
{
    return m_particles.totalAlive();
}

} // namespace illusion
