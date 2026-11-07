#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>

using namespace illusion;

// Budget: ns per live particle per update, roughly 5-10x a 12-thread desktop result; see Budget.hpp.
ILL_BENCHMARK("particles/update", 20.0)
{
    constexpr u32 kEmitters = 64;
    constexpr u32 kParticles = 2048; // per emitter, at the steady state below
    JobSystem jobs;
    World world;
    ParticleWorld particles(world);
    for (u32 i = 0; i < kEmitters; ++i)
    {
        ParticleEmitter emitter;
        emitter.maxParticles = kParticles;
        emitter.rate = 2048.0f;
        emitter.lifetimeMin = 1.0f;
        emitter.lifetimeMax = 1.0f;
        emitter.speedMin = 1.0f;
        emitter.speedMax = 3.0f;
        emitter.gravityFactor = 1.0f;
        emitter.drag = 0.5f;
        emitter.seed = i + 1;
        Transform transform;
        transform.position = Vec3(static_cast<f32>(i), 0.0f, 0.0f);
        world.create(transform, emitter);
    }
    for (int i = 0; i < 120; ++i)
    {
        particles.update(1.0f / 60.0f, &jobs); // reach the steady state: about a second of particles
    }
    const u64 alive = particles.totalAlive();
    bench.batch(alive).unit("particle").run("ParticleWorld::update", [&] {
        particles.update(1.0f / 60.0f, &jobs);
        ankerl::nanobench::doNotOptimizeAway(particles.totalAlive());
    });
}
