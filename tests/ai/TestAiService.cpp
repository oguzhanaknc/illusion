// AiWorldService (ADR 0062): the scripting API forwards to its AiWorld, returns every AiError instead of
// asserting, and gives game code the actions, the blackboards, the noises and the perceived Entities.

#include "TestSupport.hpp"

#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorld.hpp>
#include <illusion/ai/AiWorldService.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <string_view>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

constexpr std::string_view kTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "flag", "type": "Bool"}, {"name": "count", "type": "Int", "default": 2},
                 {"name": "speed", "type": "Float"}, {"name": "goal", "type": "Vector3"},
                 {"name": "friend", "type": "Entity"}],
  "root": {"sequence": [{"name": "biting", "action": "bite"}, {"name": "resting", "wait": {"seconds": 100}}]}})";

struct ServiceFixture
{
    ServiceFixture()
        : jobs(JobSystemDesc{.workerCount = 2, .queueCapacity = 256})
        , ai(world, navigation, &streamer, AiWorldDesc{.maxNoisesPerUpdate = 2})
        , service(ai)
    {
        registerNavComponents(ComponentRegistry::instance());
        registerAiComponents(ComponentRegistry::instance());
        REQUIRE(files.mount("game", directory.path, MountAccess::ReadWrite) == FileError::None);
        REQUIRE(files.writeText("game:/creature.behavior", kTree) == FileError::None);
        creature = agent("game:/creature.behavior");
        broken = agent("game:/missing.behavior");
        AiPerception ears;
        ears.sightRadius = 0.0f;
        world.set(creature, ears);
        bystander = world.create(Transform{});
        ai.waitIdle();
    }

    Entity agent(std::string_view tree)
    {
        BehaviorAgent behavior;
        behavior.tree = AssetRef(tree);
        return world.create(Transform{}, behavior);
    }

    void step(u32 count = 1)
    {
        for (u32 i = 0; i < count; ++i)
        {
            ai.update(kStep, &jobs, nullptr, true);
        }
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs;
    AssetStreamer streamer{files, jobs};
    World world;
    NavWorld navigation{world, nullptr};
    AiWorld ai;
    AiWorldService service;
    Entity creature;
    Entity broken;
    Entity bystander;
};

} // namespace

TEST_CASE("every AiError has a stable name")
{
    CHECK(aiErrorName(AiError::NoAgent) == "NoAgent");
    CHECK(aiErrorName(AiError::NoTree) == "NoTree");
    CHECK(aiErrorName(AiError::UnknownKey) == "UnknownKey");
    CHECK(aiErrorName(AiError::TypeMismatch) == "TypeMismatch");
    CHECK(aiErrorName(AiError::UnknownAction) == "UnknownAction");
    CHECK(aiErrorName(AiError::InvalidValue) == "InvalidValue");
    CHECK(aiErrorName(AiError::Full) == "Full");
}

TEST_CASE("the service writes and reads every blackboard type of an agent's tree")
{
    ServiceFixture f;
    AiService& ai = f.service;
    CHECK(ai.value(f.creature, "count")->integer == 2);

    CHECK(ai.setBool(f.creature, "flag", true).hasValue());
    CHECK(ai.setInt(f.creature, "count", -4).hasValue());
    CHECK(ai.setFloat(f.creature, "speed", 1.5f).hasValue());
    CHECK(ai.setVector(f.creature, "goal", Vec3(1.0f, 2.0f, 3.0f)).hasValue());
    CHECK(ai.setEntity(f.creature, "friend", f.bystander).hasValue());
    CHECK(ai.value(f.creature, "flag")->flag);
    CHECK(ai.value(f.creature, "count")->integer == -4);
    CHECK(ai.value(f.creature, "speed")->number == 1.5f);
    CHECK(ai.value(f.creature, "goal")->set);
    CHECK(ai.value(f.creature, "goal")->vector == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(ai.value(f.creature, "friend")->entity == f.bystander);
    CHECK(ai.value(f.creature, "friend")->set);

    CHECK(ai.clearValue(f.creature, "goal").hasValue());
    CHECK(ai.clearValue(f.creature, "friend").hasValue());
    CHECK_FALSE(ai.value(f.creature, "goal")->set);
    CHECK_FALSE(ai.value(f.creature, "friend")->set);
    CHECK(ai.setEntity(f.creature, "friend", f.bystander).hasValue());
    CHECK(ai.setEntity(f.creature, "friend", kNullEntity).hasValue()); // a null Entity clears the key
    CHECK_FALSE(ai.value(f.creature, "friend")->set);
}

TEST_CASE(
    "the service reports NoAgent, NoTree, UnknownKey, TypeMismatch and InvalidValue instead of asserting")
{
    ServiceFixture f;
    AiService& ai = f.service;
    const Entity gone = f.world.create(Transform{});
    f.world.destroy(gone);
    for (const Entity entity : {Entity{}, f.bystander, gone})
    {
        CHECK(ai.setBool(entity, "flag", true).error() == AiError::NoAgent);
        CHECK(ai.clearValue(entity, "goal").error() == AiError::NoAgent);
        CHECK(ai.value(entity, "flag").error() == AiError::NoAgent);
        CHECK(ai.finishAction(entity, 1, true).error() == AiError::NoAgent);
        std::array<AiPerceived, 1> out{};
        CHECK(ai.perceived(entity, out).error() == AiError::NoAgent);
        CHECK(ai.runningNode(entity).error() == AiError::NoAgent);
        CHECK(ai.restart(entity).error() == AiError::NoAgent);
    }

    CHECK(ai.setBool(f.broken, "flag", true).error() == AiError::NoTree);
    CHECK(ai.value(f.broken, "flag").error() == AiError::NoTree);
    CHECK(ai.runningNode(f.broken).error() == AiError::NoTree);

    CHECK(ai.setBool(f.creature, "nothing", true).error() == AiError::UnknownKey);
    CHECK(ai.value(f.creature, "nothing").error() == AiError::UnknownKey);
    CHECK(ai.clearValue(f.creature, "nothing").error() == AiError::UnknownKey);
    CHECK(ai.setInt(f.creature, "flag", 1).error() == AiError::TypeMismatch);
    CHECK(ai.setVector(f.creature, "friend", Vec3(0.0f)).error() == AiError::TypeMismatch);
    CHECK(ai.clearValue(f.creature, "flag").error() == AiError::TypeMismatch);

    constexpr f32 kNan = std::numeric_limits<f32>::quiet_NaN();
    CHECK(ai.setFloat(f.creature, "speed", kNan).error() == AiError::InvalidValue);
    CHECK(ai.setVector(f.creature, "goal", Vec3(kNan, 0.0f, 0.0f)).error() == AiError::InvalidValue);
    CHECK(ai.reportNoise({.position = Vec3(0.0f), .radius = -1.0f, .instigator = kNullEntity, .team = 0})
              .error() == AiError::InvalidValue);
    CHECK(ai.reportNoise(
                {.position = Vec3(0.0f), .radius = 1.0f, .instigator = kNullEntity, .team = kMaxAiTeams})
              .error() == AiError::InvalidValue);
    CHECK(ai.value(f.creature, "speed")->number == 0.0f);
}

TEST_CASE("actions are polled in agent order, finished by id and aborted by a restart")
{
    ServiceFixture f;
    AiService& ai = f.service;
    CHECK(ai.runningNode(f.creature)->empty()); // nothing ran before the first update

    f.step();
    std::array<AiActionRequest, 4> actions{};
    REQUIRE(ai.pollActions(actions) == 1);
    CHECK(actions[0].entity == f.creature);
    CHECK(actions[0].name == "bite");
    CHECK(actions[0].phase == AiActionPhase::Started);
    CHECK(ai.pollActions(actions) == 1); // every call of an update returns the same actions
    CHECK(ai.pollActions(std::span<AiActionRequest>{}) == 1);
    CHECK(*ai.runningNode(f.creature) == "biting");

    CHECK(ai.finishAction(f.creature, actions[0].action + 1, true).error() == AiError::UnknownAction);
    CHECK(ai.finishAction(f.creature, actions[0].action, true).hasValue());
    f.step();
    CHECK(*ai.runningNode(f.creature) == "resting");
    CHECK(ai.pollActions(actions) == 0);
    CHECK(ai.finishAction(f.creature, actions[0].action, true).error() == AiError::UnknownAction);

    CHECK(ai.restart(f.creature).hasValue());
    f.step();
    REQUIRE(ai.pollActions(actions) == 1);
    CHECK(actions[0].phase == AiActionPhase::Started);
    CHECK(*ai.runningNode(f.creature) == "biting");
    CHECK(ai.restart(f.creature).hasValue());
    f.step();
    std::array<AiActionRequest, 4> restarted{};
    const u32 count = ai.pollActions(restarted);
    REQUIRE(count >= 1);
    CHECK(restarted[0].phase == AiActionPhase::Aborted);
}

TEST_CASE("a reported noise is heard by a listener in the next update and a full update says Full")
{
    ServiceFixture f;
    AiService& ai = f.service;
    std::array<AiPerceived, 4> out{};
    CHECK(ai.perceived(f.creature, out).value() == 0);

    const AiNoise noise{
        .position = Vec3(2.0f, 1.6f, 0.0f), .radius = 6.0f, .instigator = f.bystander, .team = 0};
    CHECK(ai.reportNoise(noise).hasValue());
    CHECK(ai.reportNoise(noise).hasValue());
    CHECK(ai.reportNoise(noise).error() == AiError::Full);
    f.step();
    REQUIRE(ai.perceived(f.creature, out).value() == 1);
    CHECK(out[0].entity == f.bystander);
    CHECK(out[0].sense == AiSense::Hearing);
    CHECK(out[0].current);
    CHECK(ai.reportNoise(noise).hasValue()); // the next update has room again
}
