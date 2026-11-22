// Perception of AiWorld (ADR 0062): sight within the cone and radius, blocked by walls through physics
// raycasts, hearing within the scaled radius, memory that forgets, the chosen target and the raycast budget
// served to every perceiver in turn.

#include "TestSupport.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorld.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <doctest/doctest.h>

#include <array>
#include <numbers>
#include <span>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

struct PerceptionFixture
{
    explicit PerceptionFixture(const AiWorldDesc& desc = {})
        : ai(world, navigation, nullptr, desc)
    {
        reflectCoreComponents();
        reflectPhysicsComponents();
        registerNavComponents(ComponentRegistry::instance());
        registerAiComponents(ComponentRegistry::instance());
    }

    // A perceiver at @p position looking along +z (turned by @p yaw about +y).
    Entity perceiver(Vec3 position, f32 yaw = 0.0f, AiPerception perception = {})
    {
        Transform transform = test::at(position);
        transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
        return world.create(transform, perception);
    }

    Entity stimulus(Vec3 position, u32 team = 0)
    {
        return world.create(test::at(position),
                            AiStimulus{.team = team, .offset = Vec3(0.0f, 1.0f, 0.0f), .visible = true});
    }

    void step(u32 count = 1, f32 seconds = kStep)
    {
        for (u32 i = 0; i < count; ++i)
        {
            physics.update(static_cast<f64>(seconds));
            ai.update(seconds, &jobs, &physics, true);
        }
    }

    u32 perceivedCount(Entity entity) const
    {
        std::array<AiPerceived, kMaxPerceived> out{};
        const Expected<u32, AiError> count = ai.perceived(entity, out);
        REQUIRE(count.hasValue());
        return *count;
    }

    AiPerceived first(Entity entity) const
    {
        std::array<AiPerceived, kMaxPerceived> out{};
        const Expected<u32, AiError> count = ai.perceived(entity, out);
        REQUIRE(count.hasValue());
        REQUIRE(*count > 0);
        return out[0];
    }

    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorld physics{world, jobs};
    NavWorld navigation{world, nullptr};
    AiWorld ai;
};

} // namespace

TEST_CASE("a perceiver sees a stimulus within its cone and radius, not outside the cone or beyond the radius")
{
    PerceptionFixture f;
    const Entity eye = f.perceiver(Vec3(0.0f));
    const Entity ahead = f.stimulus(Vec3(0.0f, 0.0f, 10.0f));
    const Entity behind = f.stimulus(Vec3(0.0f, 0.0f, -5.0f));
    const Entity aside = f.stimulus(Vec3(8.0f, 0.0f, 2.0f));
    const Entity far = f.stimulus(Vec3(0.0f, 0.0f, 16.0f));
    f.step();
    REQUIRE(f.perceivedCount(eye) == 1);
    const AiPerceived seen = f.first(eye);
    CHECK(seen.entity == ahead);
    CHECK(seen.sense == AiSense::Sight);
    CHECK(seen.current);
    CHECK(seen.position == Vec3(0.0f, 0.0f, 10.0f));
    CHECK(f.world.get<AiAgentState>(eye).target == ahead);
    static_cast<void>(behind);
    static_cast<void>(aside);
    static_cast<void>(far);

    // Turned around, it sees the one behind it.
    f.world.get<Transform>(eye).rotation = glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
    f.step();
    CHECK(f.first(eye).entity == behind);
    CHECK(f.first(eye).current);
}

TEST_CASE(
    "a wall blocks sight unless its layer is not in the occluder mask; an invisible stimulus is not seen")
{
    PerceptionFixture f;
    RigidBody wallBody = test::body(MotionType::Static);
    wallBody.layer = 2;
    f.world.create(test::at(Vec3(0.0f, 1.5f, 5.0f)), wallBody, test::box(Vec3(3.0f, 1.5f, 0.2f)));
    const Entity eye = f.perceiver(Vec3(0.0f));
    const Entity target = f.stimulus(Vec3(0.0f, 0.0f, 10.0f));
    f.step();
    CHECK(f.perceivedCount(eye) == 0);
    CHECK(f.ai.statistics().rays == 1);

    f.world.get<AiPerception>(eye).occluderMask = ~(1u << 2);
    f.step();
    REQUIRE(f.perceivedCount(eye) == 1);
    CHECK(f.first(eye).entity == target);

    // Beside the wall, with the wall blocking again.
    f.world.get<AiPerception>(eye).occluderMask = ~0u;
    f.world.get<Transform>(target).position = Vec3(7.0f, 0.0f, 10.0f);
    f.step();
    CHECK(f.first(eye).current);

    f.world.get<AiStimulus>(target).visible = false;
    f.step();
    CHECK_FALSE(f.first(eye).current);
}

TEST_CASE("without physics everything in the cone is seen and no ray is cast")
{
    PerceptionFixture f;
    f.world.create(test::at(Vec3(0.0f, 1.5f, 5.0f)), test::body(MotionType::Static),
                   test::box(Vec3(3.0f, 1.5f, 0.2f)));
    const Entity eye = f.perceiver(Vec3(0.0f));
    f.stimulus(Vec3(0.0f, 0.0f, 10.0f));
    f.physics.update(static_cast<f64>(kStep));
    f.ai.update(kStep, &f.jobs, nullptr, true);
    CHECK(f.perceivedCount(eye) == 1);
    CHECK(f.ai.statistics().rays == 0);
}

TEST_CASE(
    "a noise is heard within its radius times the hearing multiplier, of a detected team, not by the deaf")
{
    PerceptionFixture f;
    AiPerception blind;
    blind.sightRadius = 0.0f;
    blind.hearing = 0.5f;
    blind.detectTeams = 0b01u;
    const Entity listener = f.perceiver(Vec3(0.0f), 0.0f, blind);
    AiPerception deaf = blind;
    deaf.hearing = 0.0f;
    const Entity stone = f.perceiver(Vec3(1.0f, 0.0f, 0.0f), 0.0f, deaf);
    const Entity maker = f.world.create(test::at(Vec3(0.0f, 0.0f, -4.0f)));
    const Vec3 ear(0.0f, blind.eyeHeight, 0.0f);

    REQUIRE(
        f.ai.reportNoise(
                {.position = ear + Vec3(0.0f, 0.0f, -4.0f), .radius = 10.0f, .instigator = maker, .team = 0})
            .hasValue());
    f.step();
    REQUIRE(f.perceivedCount(listener) == 1);
    CHECK(f.first(listener).entity == maker);
    CHECK(f.first(listener).sense == AiSense::Hearing);
    CHECK(f.first(listener).position == ear + Vec3(0.0f, 0.0f, -4.0f));
    CHECK(f.perceivedCount(stone) == 0);

    // A noise lasts one update; one beyond 10 * 0.5 m and one of another team are not heard.
    f.step(2);
    CHECK_FALSE(f.first(listener).current);
    const Entity other = f.world.create(test::at(Vec3(0.0f)));
    REQUIRE(
        f.ai.reportNoise(
                {.position = ear + Vec3(5.5f, 0.0f, 0.0f), .radius = 10.0f, .instigator = other, .team = 0})
            .hasValue());
    REQUIRE(f.ai.reportNoise({.position = ear, .radius = 10.0f, .instigator = other, .team = 1}).hasValue());
    f.step();
    CHECK(f.perceivedCount(listener) == 1);
    CHECK(f.first(listener).entity == maker);

    CHECK(f.ai.reportNoise({.position = ear, .radius = -1.0f, .instigator = other, .team = 0}).error() ==
          AiError::InvalidValue);
    CHECK(f.ai.reportNoise({.position = ear, .radius = 1.0f, .instigator = other, .team = kMaxAiTeams})
              .error() == AiError::InvalidValue);
}

TEST_CASE("the noises of an update are limited and a full update fails with Full")
{
    PerceptionFixture f(AiWorldDesc{.maxAgents = 16,
                                    .maxSightRaysPerUpdate = 16,
                                    .maxSightCandidates = 4,
                                    .maxNoisesPerUpdate = 2,
                                    .maxActions = 16,
                                    .seed = 0});
    const AiNoise noise{.position = Vec3(0.0f), .radius = 1.0f, .instigator = kNullEntity, .team = 0};
    CHECK(f.ai.reportNoise(noise).hasValue());
    CHECK(f.ai.reportNoise(noise).hasValue());
    CHECK(f.ai.reportNoise(noise).error() == AiError::Full);
    f.step();
    CHECK(f.ai.reportNoise(noise).hasValue());
}

TEST_CASE("memory keeps what is no longer sensed for memorySeconds, then forgets it")
{
    PerceptionFixture f;
    AiPerception perception;
    perception.memorySeconds = 0.5f;
    const Entity eye = f.perceiver(Vec3(0.0f), 0.0f, perception);
    const Entity target = f.stimulus(Vec3(0.0f, 0.0f, 6.0f));
    f.step();
    REQUIRE(f.first(eye).current);

    f.world.get<Transform>(target).position = Vec3(0.0f, 0.0f, -6.0f);
    f.step(1, 0.1f);
    AiPerceived memory = f.first(eye);
    CHECK_FALSE(memory.current);
    CHECK(memory.position == Vec3(0.0f, 0.0f, 6.0f));
    CHECK(memory.age == doctest::Approx(0.1f));
    CHECK(f.world.get<AiAgentState>(eye).target == target);

    f.step(3, 0.1f);
    CHECK(f.first(eye).age == doctest::Approx(0.4f));
    f.step(2, 0.1f);
    CHECK(f.perceivedCount(eye) == 0);
    CHECK(f.world.get<AiAgentState>(eye).target == kNullEntity);
}

TEST_CASE("the chosen target is the nearest sensed one and perceived lists nearest first")
{
    PerceptionFixture f;
    const Entity eye = f.perceiver(Vec3(0.0f));
    const Entity far = f.stimulus(Vec3(1.0f, 0.0f, 12.0f));
    const Entity near = f.stimulus(Vec3(-1.0f, 0.0f, 4.0f));
    f.step();
    std::array<AiPerceived, 1> one{};
    const Expected<u32, AiError> count = f.ai.perceived(eye, one);
    REQUIRE(count.hasValue());
    CHECK(*count == 2);
    CHECK(one[0].entity == near);
    CHECK(f.world.get<AiAgentState>(eye).target == near);

    f.world.destroy(near);
    f.step();
    CHECK(f.perceivedCount(eye) == 1);
    CHECK(f.world.get<AiAgentState>(eye).target == far);
    CHECK(f.ai.perceived(f.world.create(), one).error() == AiError::NoAgent);
}

TEST_CASE("the raycast budget serves every perceiver in turn, and an unserved one keeps its last result")
{
    PerceptionFixture f(AiWorldDesc{.maxAgents = 64,
                                    .maxSightRaysPerUpdate = 2,
                                    .maxSightCandidates = 4,
                                    .maxNoisesPerUpdate = 8,
                                    .maxActions = 16,
                                    .seed = 0});
    std::array<Entity, 6> eyes{};
    for (u32 i = 0; i < eyes.size(); ++i)
    {
        eyes[i] = f.perceiver(Vec3(static_cast<f32>(i) * 40.0f, 0.0f, 0.0f));
        f.stimulus(Vec3(static_cast<f32>(i) * 40.0f, 0.0f, 5.0f));
    }
    std::array<u32, 3> seen{};
    for (u32 update = 0; update < seen.size(); ++update)
    {
        f.step();
        CHECK(f.ai.statistics().rays == 2);
        for (const Entity eye : eyes)
        {
            seen[update] += f.perceivedCount(eye) > 0 ? 1u : 0u;
        }
    }
    CHECK(seen[0] == 2);
    CHECK(seen[1] == 4);
    CHECK(seen[2] == 6);

    // The next rounds keep seeing: a perceiver not served this update keeps what it saw.
    for (u32 update = 0; update < 6; ++update)
    {
        f.step();
        for (const Entity eye : eyes)
        {
            CHECK(f.first(eye).current);
        }
    }
}

TEST_CASE("Inactive perceivers and stimuli take no part")
{
    PerceptionFixture f;
    const Entity eye = f.perceiver(Vec3(0.0f));
    const Entity target = f.stimulus(Vec3(0.0f, 0.0f, 6.0f));
    f.world.emplace<Inactive>(target);
    f.step();
    CHECK(f.perceivedCount(eye) == 0);
    f.world.remove<Inactive>(target);
    f.world.emplace<Inactive>(eye);
    f.step();
    CHECK(f.perceivedCount(eye) == 0);
    f.world.remove<Inactive>(eye);
    f.step();
    CHECK(f.perceivedCount(eye) == 1);
}

TEST_CASE("drawDebug draws the sight cone, lines to what is seen, memories and noises")
{
    struct Sink final : NavDebugSink
    {
        u32 lines = 0;
        u32 seen = 0;
        void line(const Vec3&, const Vec3&, const Vec4& color) override
        {
            ++lines;
            seen += color.r == 1.0f && color.g < 0.5f ? 1u : 0u;
        }
    };
    PerceptionFixture f;
    f.perceiver(Vec3(0.0f));
    f.stimulus(Vec3(0.0f, 0.0f, 6.0f));
    REQUIRE(f.ai.reportNoise({.position = Vec3(0.0f), .radius = 3.0f, .instigator = kNullEntity, .team = 0})
                .hasValue());
    f.step();
    Sink sink;
    f.ai.drawDebug(AiDebugOptions{.sight = true, .hearing = true, .memory = true}, sink);
    CHECK(sink.seen == 1);
    CHECK(sink.lines > 40);
    Sink none;
    f.ai.drawDebug(AiDebugOptions{.sight = false, .hearing = false, .memory = false}, none);
    CHECK(none.lines == 0);
}
