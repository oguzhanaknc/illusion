#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace illusion
{

namespace
{

constexpr f32 kMinLifetime = 1e-3f;

// What one update of one emitter needs besides the emitter and its pool.
struct Step
{
    Vec3 gravity;
    f32 seconds;
    const Wind* wind; // the scene's wind, or nullptr
    f32 windTime;
};

// The wind velocity at the emitter, in the particles' space.
Vec3 stepWind(const ParticleEmitter& emitter, const Transform& transform, const Step& step) noexcept
{
    if (step.wind == nullptr || emitter.windFactor <= 0.0f)
    {
        return Vec3(0.0f);
    }
    const Vec3 velocity = windVelocity(*step.wind, step.windTime, transform.position);
    return emitter.space == ParticleSpace::Local ? glm::inverse(transform.rotation) * velocity : velocity;
}

f32 lerp(f32 a, f32 b, f32 t) noexcept
{
    return a + (b - a) * t;
}

// A direction within @p spread radians of +Y, uniform over that cap of the sphere.
Vec3 directionWithin(f32 spread, Random& random) noexcept
{
    const f32 cosMax = std::cos(std::clamp(spread, 0.0f, kParticleFullSpread));
    const f32 cosTheta = 1.0f - random.uniformF32() * (1.0f - cosMax);
    const f32 sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const f32 phi = 2.0f * std::numbers::pi_v<f32> * random.uniformF32();
    return Vec3(sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi));
}

// Where a particle is born, in the emitter's unscaled local space.
Vec3 birthOffset(const ParticleEmitter& emitter, Random& random) noexcept
{
    switch (emitter.shape)
    {
    case EmitterShape::Point:
        break;
    case EmitterShape::Sphere:
    {
        // A uniform direction and a radius that makes the points uniform in the ball.
        const Vec3 direction = directionWithin(kParticleFullSpread, random);
        return direction * (std::max(emitter.radius, 0.0f) * std::cbrt(random.uniformF32()));
    }
    case EmitterShape::Box:
    {
        const f32 x = random.uniformF32() * 2.0f - 1.0f;
        const f32 y = random.uniformF32() * 2.0f - 1.0f;
        const f32 z = random.uniformF32() * 2.0f - 1.0f;
        return Vec3(x, y, z) * glm::max(emitter.boxExtents, Vec3(0.0f));
    }
    }
    return Vec3(0.0f);
}

void resize(ParticlePool& pool, u32 capacity)
{
    pool.data = std::make_unique<f32[]>(static_cast<usize>(capacity) * ParticlePool::kFloatsPerParticle);
    pool.capacity = capacity;
    pool.count = 0;
}

void emitParticles(const ParticleEmitter& emitter, ParticlePool& pool, const Transform& transform,
                   u32 particles)
{
    const bool world = emitter.space == ParticleSpace::World;
    for (u32 n = 0; n < particles && pool.count < pool.capacity; ++n)
    {
        const Vec3 offset = birthOffset(emitter, pool.random);
        const Vec3 direction = directionWithin(emitter.spread, pool.random);
        const f32 speed = lerp(emitter.speedMin, emitter.speedMax, pool.random.uniformF32());
        const f32 lifetime =
            std::max(lerp(emitter.lifetimeMin, emitter.lifetimeMax, pool.random.uniformF32()), kMinLifetime);
        const Vec3 position =
            world ? transform.position + transform.rotation * (offset * transform.scale) : offset;
        const Vec3 velocity = world ? transform.rotation * (direction * speed) : direction * speed;
        const u32 i = pool.count++;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            pool.positions(axis)[i] = position[static_cast<glm::length_t>(axis)];
            pool.velocities(axis)[i] = velocity[static_cast<glm::length_t>(axis)];
        }
        pool.ages()[i] = 0.0f;
        pool.lifetimes()[i] = lifetime;
    }
}

// Ages, moves and retires the live particles; the dead are replaced by the last live one.
void advance(const ParticleEmitter& emitter, ParticlePool& pool, const Vec3& gravity, const Vec3& wind,
             f32 seconds)
{
    const Vec3 acceleration = gravity * emitter.gravityFactor;
    const f32 keep = std::max(0.0f, 1.0f - std::max(emitter.drag, 0.0f) * seconds);
    // The share of the gap to the wind's velocity made up in this step.
    const f32 follow = std::clamp(emitter.windFactor * seconds, 0.0f, 1.0f);
    std::span<f32> age = pool.ages();
    std::span<const f32> lifetime = pool.lifetimes();
    std::array<std::span<f32>, 3> position{pool.positions(0), pool.positions(1), pool.positions(2)};
    std::array<std::span<f32>, 3> velocity{pool.velocities(0), pool.velocities(1), pool.velocities(2)};
    for (u32 i = 0; i < pool.count;)
    {
        age[i] += seconds;
        if (age[i] >= lifetime[i])
        {
            const u32 last = --pool.count;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                position[axis][i] = position[axis][last];
                velocity[axis][i] = velocity[axis][last];
            }
            age[i] = age[last];
            pool.lifetimes()[i] = lifetime[last];
            continue;
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            const auto a = static_cast<glm::length_t>(axis);
            f32 v = (velocity[axis][i] + acceleration[a] * seconds) * keep;
            v += (wind[a] - v) * follow;
            velocity[axis][i] = v;
            position[axis][i] += v * seconds;
        }
        ++i;
    }
}

// Particles the emitter owes for @p seconds of emission (its rate over the part of the step inside its
// duration), advancing its clock; stops a timed emitter at the end of its duration.
u32 owedParticles(const ParticleEmitter& emitter, ParticlePool& pool, f32 seconds)
{
    f32 emitting = seconds;
    if (!emitter.loop)
    {
        const f64 left = static_cast<f64>(std::max(emitter.duration, 0.0f)) - pool.elapsed;
        emitting = static_cast<f32>(std::clamp(left, 0.0, static_cast<f64>(seconds)));
    }
    pool.elapsed += static_cast<f64>(seconds);
    // In f64 with a little slack, so that 60 steps of 10/60 particles make exactly 10.
    pool.pending += static_cast<f64>(std::max(emitter.rate, 0.0f)) * static_cast<f64>(emitting);
    const f64 whole = std::floor(pool.pending + 1e-9);
    pool.pending = std::max(pool.pending - whole, 0.0);
    if (!emitter.loop && pool.elapsed >= static_cast<f64>(emitter.duration))
    {
        pool.playing = false;
    }
    return static_cast<u32>(std::min(whole, static_cast<f64>(kMaxParticlesPerEmitter)));
}

// A GPU emitter: publish this step's births and time for the renderers to simulate (ADR 0047).
void publishGpuStep(const ParticleEmitter& emitter, ParticlePool& pool, const Transform& transform,
                    const Step& step)
{
    pool.capacity = std::clamp(emitter.maxParticles, 1u, kMaxParticlesPerEmitter);
    u32 births = std::exchange(pool.gpuQueued, 0u);
    if (pool.playing)
    {
        if (!pool.started)
        {
            pool.started = true;
            births += emitter.burst;
        }
        births += owedParticles(emitter, pool, step.seconds);
    }
    pool.gpuBirths = std::min(births, pool.capacity);
    pool.gpuSeconds = step.seconds;
    pool.gpuGravity = emitter.space == ParticleSpace::Local ? glm::inverse(transform.rotation) * step.gravity
                                                            : step.gravity;
    pool.gpuWind = stepWind(emitter, transform, step);
    ++pool.gpuStep;
}

void simulate(const ParticleEmitter& emitter, ParticlePool& pool, const Transform& transform,
              const Step& step)
{
    if (emitter.simulation == ParticleSimulation::Gpu)
    {
        publishGpuStep(emitter, pool, transform, step);
        return;
    }
    const u32 capacity = std::clamp(emitter.maxParticles, 1u, kMaxParticlesPerEmitter);
    if (pool.capacity != capacity || !pool.data) // a GPU emitter that became a CPU one has no arrays
    {
        resize(pool, capacity);
    }
    if (!pool.started)
    {
        // Every start draws the same numbers, so play() repeats the effect exactly.
        pool.random = Random(emitter.seed);
    }
    // Local particles feel gravity in the emitter's space.
    const Vec3 gravity = emitter.space == ParticleSpace::Local
                             ? glm::inverse(transform.rotation) * step.gravity
                             : step.gravity;
    advance(emitter, pool, gravity, stepWind(emitter, transform, step), step.seconds);
    if (!pool.playing)
    {
        return;
    }
    if (!pool.started)
    {
        pool.started = true;
        emitParticles(emitter, pool, transform, emitter.burst);
    }
    emitParticles(emitter, pool, transform, owedParticles(emitter, pool, step.seconds));
}

ParticlePool newPool(const ParticleEmitter& emitter, bool playing)
{
    ParticlePool pool;
    pool.playing = playing;
    pool.random = Random(emitter.seed);
    return pool;
}

} // namespace

ParticleWorld::ParticleWorld(World& world, const ParticleWorldDesc& desc)
    : m_world(world)
    , m_desc(desc)
{
}

void ParticleWorld::update(f32 deltaSeconds, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    // Pools are added outside the queries below; the list keeps its capacity between updates.
    m_missing.clear();
    m_world.query<const ParticleEmitter>().without<ParticlePool>().each(
        [this](Entity entity, const ParticleEmitter&) { m_missing.push_back(entity); });
    for (const Entity entity : m_missing)
    {
        const ParticleEmitter& emitter = m_world.get<ParticleEmitter>(entity);
        m_world.emplace<ParticlePool>(entity, newPool(emitter, emitter.playOnStart));
    }

    // The scene's wind: the first active one (its clock is advanced by advanceWind()).
    const Wind* wind = nullptr;
    f32 windTime = 0.0f;
    m_world.query<const Wind>().without<Inactive>().each([&](Entity entity, const Wind& candidate) {
        if (wind == nullptr)
        {
            wind = &candidate;
            const WindState* state = m_world.tryGet<WindState>(entity);
            windTime = state != nullptr ? state->time : 0.0f;
        }
    });
    const Step step{m_desc.gravity, std::max(deltaSeconds, 0.0f), wind, windTime};
    auto query = m_world.query<const ParticleEmitter, ParticlePool, const Transform>().without<Inactive>();
    const Step* shared = &step;
    const auto run = [shared](const ParticleEmitter& emitter, ParticlePool& pool,
                              const Transform& transform) {
        simulate(emitter, pool, transform, *shared);
    };
    if (jobs != nullptr)
    {
        query.parallelEach(*jobs, run);
    }
    else
    {
        query.each(run);
    }
    u64 alive = 0;
    m_world.query<const ParticlePool>().each([&alive](const ParticlePool& pool) { alive += pool.count; });
    m_totalAlive = alive;
}

bool ParticleWorld::play(Entity entity)
{
    const ParticleEmitter* emitter = m_world.tryGet<ParticleEmitter>(entity);
    if (emitter == nullptr)
    {
        return false;
    }
    ParticlePool* pool = m_world.tryGet<ParticlePool>(entity);
    if (pool == nullptr)
    {
        m_world.emplace<ParticlePool>(entity, newPool(*emitter, true));
        return true;
    }
    pool->count = 0;
    pool->elapsed = 0.0;
    pool->pending = 0.0;
    pool->started = false;
    pool->playing = true;
    pool->gpuQueued = 0;
    ++pool->gpuGeneration;
    return true;
}

bool ParticleWorld::stop(Entity entity)
{
    const ParticleEmitter* emitter = m_world.tryGet<ParticleEmitter>(entity);
    if (emitter == nullptr)
    {
        return false;
    }
    if (ParticlePool* pool = m_world.tryGet<ParticlePool>(entity))
    {
        pool->playing = false;
        return true;
    }
    m_world.emplace<ParticlePool>(entity, newPool(*emitter, false));
    return true;
}

u32 ParticleWorld::emit(Entity entity, u32 count)
{
    const ParticleEmitter* emitter = m_world.tryGet<ParticleEmitter>(entity);
    const Transform* transform = m_world.tryGet<Transform>(entity);
    if (emitter == nullptr || transform == nullptr)
    {
        return 0;
    }
    if (!m_world.has<ParticlePool>(entity))
    {
        m_world.emplace<ParticlePool>(entity, newPool(*emitter, emitter->playOnStart));
    }
    ParticlePool& pool = m_world.get<ParticlePool>(entity);
    const u32 capacity = std::clamp(emitter->maxParticles, 1u, kMaxParticlesPerEmitter);
    if (emitter->simulation == ParticleSimulation::Gpu)
    {
        // Published with the next step; the live count the renderer reports bounds what fits.
        pool.capacity = capacity;
        const u32 room = capacity - std::min(capacity, pool.count + pool.gpuQueued);
        const u32 queued = std::min(count, room);
        pool.gpuQueued += queued;
        return queued;
    }
    if (pool.capacity != capacity || !pool.data) // a GPU emitter that became a CPU one has no arrays
    {
        resize(pool, capacity);
    }
    const u32 before = pool.count;
    emitParticles(*emitter, pool, *transform, count);
    return pool.count - before;
}

bool ParticleWorld::clear(Entity entity)
{
    ParticlePool* pool = m_world.tryGet<ParticlePool>(entity);
    if (pool == nullptr)
    {
        return false;
    }
    pool->count = 0;
    pool->gpuQueued = 0;
    ++pool->gpuGeneration;
    return true;
}

u32 ParticleWorld::aliveCount(Entity entity) const
{
    const ParticlePool* pool = m_world.tryGet<ParticlePool>(entity);
    return pool != nullptr ? pool->count : 0;
}

bool ParticleWorld::isPlaying(Entity entity) const
{
    if (const ParticlePool* pool = m_world.tryGet<ParticlePool>(entity))
    {
        return pool->playing;
    }
    const ParticleEmitter* emitter = m_world.tryGet<ParticleEmitter>(entity);
    return emitter != nullptr && emitter->playOnStart;
}

} // namespace illusion
