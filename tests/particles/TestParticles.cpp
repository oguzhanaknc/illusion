// ParticleEmitter and ParticleWorld (ADR 0047): emission, lifetimes, motion, shapes, spaces, determinism,
// play/stop and the allocation-free update.

#include "TestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>
#include <illusion/particles/ParticleWorldService.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

// An emitter that emits @p rate particles per second straight up, living @p lifetime seconds.
ParticleEmitter emitter(f32 rate = 10.0f, f32 lifetime = 1.0f)
{
    ParticleEmitter e;
    e.rate = rate;
    e.lifetimeMin = lifetime;
    e.lifetimeMax = lifetime;
    e.spread = 0.0f;
    return e;
}

Transform at(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

void run(ParticleWorld& particles, f32 seconds, JobSystem* jobs = nullptr)
{
    const int steps = static_cast<int>(std::lround(seconds / kStep));
    for (int i = 0; i < steps; ++i)
    {
        particles.update(kStep, jobs);
    }
}

std::vector<Vec3> positionsOf(const World& world, Entity entity)
{
    const ParticlePool& pool = world.get<ParticlePool>(entity);
    std::vector<Vec3> out;
    for (u32 i = 0; i < pool.count; ++i)
    {
        out.push_back(pool.position(i));
    }
    return out;
}

} // namespace

TEST_CASE("ParticleEmitter is reflected with every field under a stable name and the pool is transient")
{
    ComponentRegistry registry;
    reflectParticleComponents(registry);
    const ComponentId id = registry.findByStableName("illusion.ParticleEmitter");
    REQUIRE(id != kInvalidComponentId);
    const TypeReflection* type = registry.reflection(id);
    REQUIRE(type != nullptr);
    CHECK(type->fields.size() == 26);
    CHECK(registry.findByStableName("illusion.ParticlePool") != kInvalidComponentId);
}

TEST_CASE("an emitter emits its rate per second, fires its burst once and never exceeds its capacity")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(10.0f, 10.0f);
    e.burst = 5;
    const Entity entity = world.create(at(Vec3(0.0f)), e);
    run(particles, 1.0f);
    CHECK(particles.aliveCount(entity) == 15);
    run(particles, 1.0f);
    CHECK(particles.aliveCount(entity) == 25); // the burst fired once
    CHECK(particles.totalAlive() == 25);

    world.get<ParticleEmitter>(entity).maxParticles = 8; // a smaller capacity starts the pool afresh
    run(particles, 5.0f);
    CHECK(particles.aliveCount(entity) == 8);
    CHECK(world.get<ParticlePool>(entity).capacity == 8);
}

TEST_CASE("particles die at the end of their lifetime and the live ones stay contiguous")
{
    World world;
    ParticleWorld particles(world);
    const Entity entity = world.create(at(Vec3(0.0f)), emitter(60.0f, 0.5f));
    run(particles, 2.0f);
    // A steady state: half a second of particles at 60 per second.
    CHECK(particles.aliveCount(entity) == doctest::Approx(30).epsilon(0.07));
    const ParticlePool& pool = world.get<ParticlePool>(entity);
    for (u32 i = 0; i < pool.count; ++i)
    {
        CHECK(pool.ages()[i] < pool.lifetimes()[i]);
        CHECK(pool.lifeFraction(i) >= 0.0f);
        CHECK(pool.lifeFraction(i) < 1.0f);
    }
}

TEST_CASE("particles move with their speed, gravity and drag")
{
    World world;
    ParticleWorld particles(world, ParticleWorldDesc{.gravity = Vec3(0.0f, -10.0f, 0.0f)});
    ParticleEmitter e = emitter(0.0f, 100.0f);
    e.burst = 1;
    e.speedMin = 2.0f;
    e.speedMax = 2.0f;
    const Entity rising = world.create(at(Vec3(0.0f)), e);
    e.gravityFactor = 1.0f;
    const Entity falling = world.create(at(Vec3(5.0f, 0.0f, 0.0f)), e);
    e.gravityFactor = 0.0f;
    e.drag = 2.0f;
    const Entity slowed = world.create(at(Vec3(10.0f, 0.0f, 0.0f)), e);
    run(particles, 1.0f);
    // The burst is born in the first update and moves from the second, so 59 steps of 1/60 s.
    const f32 t = 59.0f * kStep;
    CHECK(world.get<ParticlePool>(rising).position(0).y == doctest::Approx(2.0f * t).epsilon(0.01));
    CHECK(world.get<ParticlePool>(falling).position(0).y < 0.0f);
    CHECK(world.get<ParticlePool>(slowed).position(0).y < 0.5f * 2.0f * t);
    CHECK(world.get<ParticlePool>(slowed).position(0).y > 0.0f);
    CHECK(world.get<ParticlePool>(rising).position(0).x == doctest::Approx(0.0f));
}

TEST_CASE("particles with a wind factor take on the wind's velocity, in their own space")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(0.0f, 100.0f);
    e.burst = 1;
    e.speedMin = 0.0f;
    e.speedMax = 0.0f;
    const Entity still = world.create(at(Vec3(0.0f)), e);
    e.windFactor = 2.0f;
    const Entity blown = world.create(at(Vec3(5.0f, 0.0f, 0.0f)), e);
    e.space = ParticleSpace::Local;
    Transform turned = at(Vec3(10.0f, 0.0f, 0.0f));
    turned.rotation =
        glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(0.0f, 1.0f, 0.0f)); // a quarter turn about +y
    const Entity local = world.create(turned, e);

    // Without a Wind nothing moves.
    run(particles, 0.5f);
    CHECK(world.get<ParticlePool>(blown).position(0) == Vec3(5.0f, 0.0f, 0.0f));

    world.create(Wind{.direction = Vec3(0.0f, 0.0f, 1.0f), .speed = 3.0f, .gustStrength = 0.0f});
    run(particles, 3.0f);
    CHECK(world.get<ParticlePool>(still).position(0) == Vec3(0.0f));
    // After 3 s at a factor of 2 the particle moves at nearly the wind's 3 m/s.
    const ParticlePool& pool = world.get<ParticlePool>(blown);
    CHECK(pool.velocities(2)[0] == doctest::Approx(3.0).epsilon(0.01));
    CHECK(pool.position(0).z > 6.0f);
    CHECK(pool.position(0).x == doctest::Approx(5.0));
    // A local particle feels the wind in the emitter's space: world +z is local -x after a quarter turn.
    const ParticlePool& localPool = world.get<ParticlePool>(local);
    CHECK(localPool.velocities(0)[0] == doctest::Approx(-3.0).epsilon(0.01));
    CHECK(std::abs(localPool.velocities(2)[0]) < 1e-3f);

    // GPU emitters publish the wind velocity with each step.
    e.space = ParticleSpace::World;
    e.simulation = ParticleSimulation::Gpu;
    const Entity gpu = world.create(at(Vec3(0.0f)), e);
    particles.update(kStep);
    CHECK(world.get<ParticlePool>(gpu).gpuWind.z == doctest::Approx(3.0));
}

TEST_CASE("emission shapes place particles inside them and the spread bounds their directions")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(0.0f, 10.0f);
    e.burst = 500;
    e.maxParticles = 500;
    e.speedMin = 0.0f;
    e.speedMax = 0.0f;
    e.shape = EmitterShape::Sphere;
    e.radius = 2.0f;
    const Entity sphere = world.create(at(Vec3(1.0f, 2.0f, 3.0f)), e);
    e.shape = EmitterShape::Box;
    e.boxExtents = Vec3(1.0f, 0.5f, 0.25f);
    const Entity box = world.create(at(Vec3(0.0f)), e);
    e.shape = EmitterShape::Point;
    e.speedMin = 1.0f;
    e.speedMax = 1.0f;
    e.spread = 0.5f; // about 29 degrees
    const Entity cone = world.create(at(Vec3(0.0f)), e);
    particles.update(kStep);
    particles.update(kStep);
    for (const Vec3& p : positionsOf(world, sphere))
    {
        CHECK(glm::length(p - Vec3(1.0f, 2.0f, 3.0f)) <= 2.0f + 1e-4f);
    }
    for (const Vec3& p : positionsOf(world, box))
    {
        CHECK(std::abs(p.x) <= 1.0f + 1e-4f);
        CHECK(std::abs(p.y) <= 0.5f + 1e-4f);
        CHECK(std::abs(p.z) <= 0.25f + 1e-4f);
    }
    for (const Vec3& p : positionsOf(world, cone))
    {
        const f32 cosine = glm::dot(glm::normalize(p), Vec3(0.0f, 1.0f, 0.0f));
        CHECK(cosine >= std::cos(0.5f) - 1e-4f);
    }
}

TEST_CASE("world-space particles stay behind when the emitter moves and local ones move with it")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(0.0f, 10.0f);
    e.burst = 1;
    e.speedMin = 0.0f;
    e.speedMax = 0.0f;
    const Entity worldSpace = world.create(at(Vec3(1.0f, 0.0f, 0.0f)), e);
    e.space = ParticleSpace::Local;
    const Entity localSpace = world.create(at(Vec3(1.0f, 0.0f, 0.0f)), e);
    particles.update(kStep);
    world.get<Transform>(worldSpace).position = Vec3(5.0f, 0.0f, 0.0f);
    world.get<Transform>(localSpace).position = Vec3(5.0f, 0.0f, 0.0f);
    particles.update(kStep);
    CHECK(world.get<ParticlePool>(worldSpace).position(0).x == doctest::Approx(1.0f));
    // Local particles are stored in the emitter's space: the renderer places them with its Transform.
    CHECK(world.get<ParticlePool>(localSpace).position(0).x == doctest::Approx(0.0f));
}

TEST_CASE("equal seeds give equal particles, serial or on the Job System, and other seeds differ")
{
    const auto simulate = [](u32 seed, JobSystem* jobs) {
        World world;
        ParticleWorld particles(world);
        ParticleEmitter e = emitter(100.0f, 1.0f);
        e.shape = EmitterShape::Sphere;
        e.spread = kParticleFullSpread;
        e.speedMin = 0.5f;
        e.speedMax = 3.0f;
        e.lifetimeMin = 0.5f;
        e.lifetimeMax = 1.5f;
        e.seed = seed;
        Entity last;
        for (int i = 0; i < 8; ++i)
        {
            e.seed = seed + static_cast<u32>(i);
            last = world.create(at(Vec3(static_cast<f32>(i), 0.0f, 0.0f)), e);
        }
        run(particles, 1.0f, jobs);
        return positionsOf(world, last);
    };
    JobSystem jobs{JobSystemDesc{4, 1024}};
    const std::vector<Vec3> serial = simulate(7, nullptr);
    CHECK_FALSE(serial.empty());
    CHECK(simulate(7, &jobs) == serial);
    CHECK(simulate(8, nullptr) != serial);
}

TEST_CASE("play restarts an emitter exactly, stop ends emission and a non-looping emitter stops by itself")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(20.0f, 10.0f);
    e.loop = false;
    e.duration = 0.5f;
    e.playOnStart = false;
    const Entity entity = world.create(at(Vec3(0.0f)), e);
    run(particles, 1.0f);
    CHECK(particles.aliveCount(entity) == 0);
    CHECK_FALSE(particles.isPlaying(entity));

    CHECK(particles.play(entity));
    run(particles, 1.0f);
    CHECK(particles.aliveCount(entity) == 10); // half a second at 20 per second
    CHECK_FALSE(particles.isPlaying(entity));
    const std::vector<Vec3> first = positionsOf(world, entity);

    CHECK(particles.play(entity));
    run(particles, 1.0f);
    CHECK(positionsOf(world, entity) == first);

    world.get<ParticleEmitter>(entity).loop = true;
    CHECK(particles.play(entity));
    run(particles, 0.25f);
    CHECK(particles.stop(entity));
    const u32 stopped = particles.aliveCount(entity);
    run(particles, 1.0f);
    CHECK(particles.aliveCount(entity) == stopped); // they live 10 s, none is added
    CHECK(particles.clear(entity));
    CHECK(particles.aliveCount(entity) == 0);

    const Entity none = world.create(at(Vec3(0.0f)));
    CHECK_FALSE(particles.play(none));
    CHECK_FALSE(particles.stop(none));
    CHECK_FALSE(particles.clear(none));
}

TEST_CASE("emitters on Inactive entities pause")
{
    World world;
    ParticleWorld particles(world);
    const Entity entity = world.create(at(Vec3(0.0f)), emitter(60.0f, 10.0f));
    run(particles, 0.5f);
    const u32 before = particles.aliveCount(entity);
    world.emplace<Inactive>(entity);
    run(particles, 0.5f);
    CHECK(particles.aliveCount(entity) == before);
}

TEST_CASE("colour and size follow the particle's life from start to end")
{
    ParticleEmitter e;
    e.colorStart = Vec4(4.0f, 2.0f, 1.0f, 1.0f);
    e.colorEnd = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    e.sizeStart = 0.2f;
    e.sizeEnd = 1.0f;
    CHECK(particleColor(e, 0.0f) == e.colorStart);
    CHECK(particleColor(e, 0.5f) == Vec4(2.0f, 1.0f, 0.5f, 0.5f));
    CHECK(particleColor(e, 2.0f) == e.colorEnd);
    CHECK(particleSize(e, 0.5f) == doctest::Approx(0.6f));
    CHECK(particleSize(e, -1.0f) == doctest::Approx(0.2f));
}

TEST_CASE("updates do not allocate once the pools exist")
{
    World world;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    ParticleWorld particles(world);
    for (int i = 0; i < 16; ++i)
    {
        ParticleEmitter e = emitter(200.0f, 0.5f);
        e.maxParticles = 1024;
        e.gravityFactor = 1.0f;
        world.create(at(Vec3(static_cast<f32>(i), 0.0f, 0.0f)), e);
    }
    run(particles, 0.5f, &jobs);
    const test::ScopedAllocationCounter allocations;
    run(particles, 1.0f, &jobs);
    run(particles, 1.0f);
    CHECK(allocations.count() == 0);
}

TEST_CASE("emit adds a burst at the emitter now, playing or not, up to its capacity")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(0.0f, 10.0f);
    e.playOnStart = false;
    e.maxParticles = 10;
    const Entity entity = world.create(at(Vec3(2.0f, 0.0f, 0.0f)), e);
    CHECK(particles.emit(entity, 4) == 4); // before any update: the pool is created
    CHECK(particles.aliveCount(entity) == 4);
    CHECK_FALSE(particles.isPlaying(entity));
    CHECK(world.get<ParticlePool>(entity).position(0).x == doctest::Approx(2.0f));
    CHECK(particles.emit(entity, 20) == 6);
    CHECK(particles.aliveCount(entity) == 10);
    run(particles, 1.0f);
    CHECK(particles.aliveCount(entity) == 10); // not playing: nothing more is emitted
    CHECK(particles.emit(world.create(at(Vec3(0.0f))), 5) == 0);
}

TEST_CASE("ParticleWorldService forwards every call to its ParticleWorld")
{
    World world;
    ParticleWorld particles(world);
    ParticleWorldService service(particles);
    CHECK(&service.particles() == &particles);
    const Entity entity = world.create(at(Vec3(0.0f)), emitter(60.0f, 10.0f));
    CHECK(service.play(entity));
    run(particles, 0.5f);
    CHECK(service.isPlaying(entity));
    CHECK(service.aliveCount(entity) == 30);
    CHECK(service.totalAlive() == 30);
    CHECK(service.emit(entity, 5) == 5);
    CHECK(service.stop(entity));
    CHECK_FALSE(service.isPlaying(entity));
    CHECK(service.clear(entity));
    CHECK(service.aliveCount(entity) == 0);
}

TEST_CASE("GPU emitters publish each step's births and time for the renderers and start afresh on play")
{
    World world;
    ParticleWorld particles(world);
    ParticleEmitter e = emitter(60.0f, 10.0f);
    e.simulation = ParticleSimulation::Gpu;
    e.burst = 5;
    e.maxParticles = 50;
    const Entity entity = world.create(at(Vec3(0.0f)), e);
    particles.update(kStep);
    ParticlePool& pool = world.get<ParticlePool>(entity);
    CHECK(pool.gpuStep == 1);
    CHECK(pool.gpuBirths == 6); // the burst and one particle of the rate
    CHECK(pool.gpuSeconds == doctest::Approx(kStep));
    CHECK(pool.data == nullptr); // no CPU particles
    CHECK(pool.count == 0);      // the renderer writes the live count
    particles.update(kStep);
    CHECK(pool.gpuStep == 2);
    CHECK(pool.gpuBirths == 1);

    // emit() is published with the next step, within the room the live count leaves.
    pool.count = 45;
    CHECK(particles.emit(entity, 20) == 5);
    CHECK(pool.gpuQueued == 5);
    particles.update(kStep);
    CHECK(pool.gpuBirths == 6);
    CHECK(pool.gpuQueued == 0);

    const u32 generation = pool.gpuGeneration;
    CHECK(particles.clear(entity));
    CHECK(pool.gpuGeneration == generation + 1);
    CHECK(particles.play(entity));
    CHECK(pool.gpuGeneration == generation + 2);
    CHECK(particles.stop(entity));
    particles.update(kStep);
    CHECK(pool.gpuBirths == 0);

    // Back on the CPU, the emitter gets its arrays.
    world.get<ParticleEmitter>(entity).simulation = ParticleSimulation::Cpu;
    CHECK(particles.play(entity));
    run(particles, 0.5f);
    CHECK(particles.aliveCount(entity) > 0);
}
