// The behaviour trees of AiWorld (ADR 0062): `.behavior` files loaded through the streamer, starting values,
// blackboard writes and their errors, actions handed to game code, trees driving NavAgents (moveTo, wander,
// a LowerPriority guard over perception), staggered ticks, Inactive agents, live reload, restart, the
// GameSession's AI step, determinism for 1 and 4 workers and a steady update without allocations.

#include "TestSupport.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/GameSession.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

constexpr std::string_view kWaitTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "flag", "type": "Bool"}, {"name": "count", "type": "Int", "default": 2},
                 {"name": "speed", "type": "Float"}, {"name": "goal", "type": "Vector3"},
                 {"name": "friend", "type": "Entity"}],
  "root": {"wait": {"seconds": 100}}})";

constexpr std::string_view kActionTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "done", "type": "Bool"}, {"name": "failed", "type": "Bool"}],
  "root": {"selector": [
    {"sequence": [{"action": "bite"}, {"setValue": {"key": "done", "value": true}}, {"wait": {"seconds": 100}}]},
    {"sequence": [{"setValue": {"key": "failed", "value": true}}, {"wait": {"seconds": 100}}]}]}})";

constexpr std::string_view kMoveTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "goal", "type": "Vector3"}, {"name": "arrived", "type": "Bool"}],
  "root": {"sequence": [{"moveTo": {"key": "goal", "acceptance": 0.5}},
                        {"setValue": {"key": "arrived", "value": true}}, {"wait": {"seconds": 100}}]}})";

constexpr std::string_view kWanderTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "home", "type": "Vector3"}],
  "root": {"sequence": [{"wander": {"key": "home", "radius": 4}}, {"wait": {"seconds": 0.2}}]}})";

constexpr std::string_view kGuardTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "target", "type": "Entity"}, {"name": "home", "type": "Vector3"}],
  "root": {"selector": [
    {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
      "child": {"sequence": [{"moveTo": {"key": "target", "acceptance": 1.5}}, {"action": "bite"},
                             {"wait": {"seconds": 0.5}}]}}},
    {"sequence": [{"wander": {"key": "home", "radius": 6}}, {"wait": {"seconds": 0.5, "deviation": 0.25}}]}]}})";

void reflectAll()
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    registerNavComponents(ComponentRegistry::instance());
    registerAiComponents(ComponentRegistry::instance());
}

Entity addBox(World& world, Vec3 center, Vec3 halfExtents)
{
    return world.create(test::at(center), test::body(MotionType::Static), test::box(halfExtents));
}

// A 40 m ground with its navmesh surface.
void buildGround(World& world)
{
    NavMeshSurface surface;
    surface.size = Vec3(40.0f, 8.0f, 40.0f);
    surface.build.cellSize = 0.25f;
    surface.build.tileSize = 64;
    world.create(test::at(Vec3(0.0f, 2.0f, 0.0f)), surface);
    addBox(world, Vec3(0.0f, -0.5f, 0.0f), Vec3(20.0f, 0.5f, 20.0f));
}

BlackboardOverride number(std::string_view key, f32 value)
{
    return BlackboardOverride{
        .key = std::string(key), .number = value, .vector = Vec3(0.0f), .entity = kNullEntity};
}

BlackboardOverride vector(std::string_view key, Vec3 value)
{
    return BlackboardOverride{
        .key = std::string(key), .number = 0.0f, .vector = value, .entity = kNullEntity};
}

struct AiFixture
{
    explicit AiFixture(u32 workers = 2, const AiWorldDesc& desc = {})
        : jobs(JobSystemDesc{.workerCount = workers, .queueCapacity = 1024})
        , ai(world, navigation, &streamer, desc)
    {
        reflectAll();
        REQUIRE(files.mount("game", directory.path, MountAccess::ReadWrite) == FileError::None);
    }

    void write(std::string_view path, std::string_view text)
    {
        REQUIRE(files.writeText(path, text) == FileError::None);
    }

    Entity agent(Vec3 position, std::string_view tree, bool navigates = true)
    {
        BehaviorAgent behavior;
        behavior.tree = AssetRef(tree);
        const Entity entity = world.create(test::at(position), behavior);
        if (navigates)
        {
            NavAgent nav;
            nav.drive = NavDrive::Transform;
            world.set(entity, nav);
        }
        return entity;
    }

    void ground()
    {
        buildGround(world);
        navigation.waitIdle(&jobs);
    }

    // AI then navigation, as in the GameSession.
    void step(u32 count = 1, f32 seconds = kStep)
    {
        for (u32 i = 0; i < count; ++i)
        {
            ai.update(seconds, &jobs, nullptr, true);
            navigation.update(seconds, &jobs, true);
        }
    }

    [[nodiscard]] BlackboardValue value(Entity entity, std::string_view key) const
    {
        const Expected<BlackboardValue, AiError> found = ai.value(entity, key);
        REQUIRE(found.hasValue());
        return *found;
    }

    [[nodiscard]] const BehaviorTreeState& state(Entity entity) const
    {
        const std::optional<u32> slot = ai.findSlot(entity);
        REQUIRE(slot.has_value());
        const BehaviorTreeState* found = ai.treeState(*slot);
        REQUIRE(found != nullptr);
        return *found;
    }

    // Type of the deepest running node, or Sequence when none runs.
    [[nodiscard]] BehaviorNodeType running(Entity entity) const
    {
        const std::optional<u32> slot = ai.findSlot(entity);
        REQUIRE(slot.has_value());
        const BehaviorTree* tree = ai.tree(*slot);
        REQUIRE(tree != nullptr);
        const i32 node = world.get<AiAgentState>(entity).runningNode;
        return node >= 0 ? tree->nodes[static_cast<usize>(node)].type : BehaviorNodeType::Sequence;
    }

    [[nodiscard]] u32 countActions(std::string_view name, AiActionPhase phase) const
    {
        u32 count = 0;
        for (const AiActionRequest& request : ai.actions())
        {
            count += request.name == name && request.phase == phase ? 1u : 0u;
        }
        return count;
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs;
    AssetStreamer streamer{files, jobs};
    World world;
    NavWorld navigation{world, nullptr};
    AiWorld ai;
};

f32 flatDistance(Vec3 a, Vec3 b)
{
    return glm::length(Vec2(a.x - b.x, a.z - b.z));
}

} // namespace

TEST_CASE("a BehaviorAgent's tree loads through the streamer and starts with its defaults and overrides")
{
    AiFixture f;
    f.write("game:/wait.behavior", kWaitTree);
    const Entity other = f.world.create(test::at(Vec3(3.0f, 0.0f, 0.0f)));
    const Entity entity = f.agent(Vec3(0.0f), "game:/wait.behavior", false);
    BehaviorAgent& agent = f.world.get<BehaviorAgent>(entity);
    agent.blackboard = {
        number("flag", 1.0f),
        number("count", 7.4f),
        number("speed", 2.5f),
        vector("goal", Vec3(1.0f, 2.0f, 3.0f)),
        BlackboardOverride{.key = "friend", .number = 0.0f, .vector = Vec3(0.0f), .entity = other},
        number("missing", 1.0f)};
    CHECK_FALSE(f.ai.findSlot(entity).has_value());
    CHECK(f.ai.value(entity, "flag").error() == AiError::NoTree);

    f.ai.waitIdle();
    REQUIRE(f.ai.findSlot(entity).has_value());
    CHECK(f.world.get<AiAgentState>(entity).slot == *f.ai.findSlot(entity));
    CHECK(f.ai.statistics().agents == 1);
    CHECK(f.ai.statistics().pending == 0);
    CHECK(f.value(entity, "flag").flag);
    CHECK(f.value(entity, "count").integer == 7);
    CHECK(f.value(entity, "speed").number == 2.5f);
    CHECK(f.value(entity, "goal").set);
    CHECK(f.value(entity, "goal").vector == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(f.value(entity, "friend").entity == other);

    BlackboardValue count = f.value(entity, "count");
    count.integer = 11;
    REQUIRE(f.ai.setValue(entity, "count", count).hasValue());
    CHECK(f.value(entity, "count").integer == 11);
    CHECK(f.ai.setValue(entity, "nothing", count).error() == AiError::UnknownKey);
    CHECK(f.ai.setValue(entity, "speed", count).error() == AiError::TypeMismatch);
    BlackboardValue speed = f.value(entity, "speed");
    speed.number = std::numeric_limits<f32>::quiet_NaN();
    CHECK(f.ai.setValue(entity, "speed", speed).error() == AiError::InvalidValue);
    CHECK(f.ai.value(other, "flag").error() == AiError::NoAgent);
    CHECK(f.ai.value(Entity{}, "flag").error() == AiError::NoAgent);
}

TEST_CASE("a missing or wrong tree file fails once and its agent has no tree")
{
    AiFixture f;
    const Entity missing = f.agent(Vec3(0.0f), "game:/missing.behavior", false);
    const Entity wrong = f.agent(Vec3(0.0f), "game:/tree.animgraph", false);
    const Entity none = f.agent(Vec3(0.0f), "", false);
    f.ai.waitIdle();
    CHECK(f.ai.statistics().failures == 2);
    CHECK(f.ai.statistics().pending == 0);
    CHECK(f.ai.value(missing, "flag").error() == AiError::NoTree);
    CHECK(f.ai.restart(wrong).error() == AiError::NoTree);
    CHECK(f.ai.finishAction(none, 1, true).error() == AiError::NoTree);
    REQUIRE(f.ai.findSlot(none).has_value());
    CHECK(f.ai.tree(*f.ai.findSlot(none)) == nullptr);
    CHECK(f.ai.treeState(*f.ai.findSlot(none)) == nullptr);
    f.step(3);
    CHECK(f.ai.statistics().failures == 2);
}

TEST_CASE("an action is handed to game code, and finishing it lets its node succeed or fail")
{
    AiFixture f;
    f.write("game:/act.behavior", kActionTree);
    const Entity winner = f.agent(Vec3(0.0f), "game:/act.behavior", false);
    const Entity loser = f.agent(Vec3(1.0f, 0.0f, 0.0f), "game:/act.behavior", false);
    f.ai.waitIdle();
    f.step();
    REQUIRE(f.ai.actions().size() == 2);
    const AiActionRequest started = f.ai.actions()[0];
    CHECK(started.entity == winner);
    CHECK(started.name == "bite");
    CHECK(started.phase == AiActionPhase::Started);
    CHECK(f.ai.actions()[1].entity == loser);
    const u32 loserAction = f.ai.actions()[1].action;
    CHECK(f.running(winner) == BehaviorNodeType::Action);
    CHECK(f.world.get<AiAgentState>(winner).status == BehaviorStatus::Running);

    // Not finished: it keeps running and is not reported again.
    f.step(3);
    CHECK(f.ai.actions().empty());
    CHECK_FALSE(f.value(winner, "done").flag);
    CHECK(f.ai.finishAction(winner, started.action + 100, true).error() == AiError::UnknownAction);

    REQUIRE(f.ai.finishAction(winner, started.action, true).hasValue());
    CHECK(f.ai.finishAction(winner, started.action, true).error() == AiError::UnknownAction);
    REQUIRE(f.ai.finishAction(loser, loserAction, false).hasValue());
    f.step();
    CHECK(f.value(winner, "done").flag);
    CHECK_FALSE(f.value(winner, "failed").flag);
    CHECK_FALSE(f.value(loser, "done").flag);
    CHECK(f.value(loser, "failed").flag);
    CHECK(f.running(winner) == BehaviorNodeType::Wait);
}

TEST_CASE("restart starts the tree from its root with the starting blackboard and aborts its running action")
{
    AiFixture f;
    f.write("game:/act.behavior", kActionTree);
    const Entity entity = f.agent(Vec3(0.0f), "game:/act.behavior", false);
    f.ai.waitIdle();
    f.step();
    REQUIRE(f.ai.actions().size() == 1);
    const u32 first = f.ai.actions()[0].action;
    BlackboardValue done = f.value(entity, "done");
    done.flag = true;
    REQUIRE(f.ai.setValue(entity, "done", done).hasValue());

    REQUIRE(f.ai.restart(entity).hasValue());
    CHECK_FALSE(f.value(entity, "done").flag);
    CHECK(f.ai.finishAction(entity, first, true).error() == AiError::UnknownAction);
    f.step();
    REQUIRE(f.ai.actions().size() == 2);
    CHECK(f.ai.actions()[0].phase == AiActionPhase::Aborted);
    CHECK(f.ai.actions()[0].action == first);
    CHECK(f.ai.actions()[0].name == "bite");
    CHECK(f.ai.actions()[1].phase == AiActionPhase::Started);
    CHECK(f.ai.actions()[1].action != first);
    CHECK(f.ai.restart(f.world.create()).error() == AiError::NoAgent);
}

TEST_CASE("a moveTo drives the NavAgent to a Vector3 key and succeeds when it arrives")
{
    AiFixture f;
    f.ground();
    f.write("game:/move.behavior", kMoveTree);
    const Entity entity = f.agent(Vec3(-6.0f, 0.0f, -6.0f), "game:/move.behavior");
    const Vec3 goal(6.0f, 0.0f, 5.0f);
    f.world.get<BehaviorAgent>(entity).blackboard = {vector("goal", goal)};
    f.ai.waitIdle();
    u32 updates = 0;
    while (updates < 900 && !f.value(entity, "arrived").flag)
    {
        f.step();
        ++updates;
        if (updates == 2)
        {
            REQUIRE(f.world.get<NavAgent>(entity).destination.has_value());
            CHECK(flatDistance(*f.world.get<NavAgent>(entity).destination, goal) < 0.01f);
            CHECK(f.running(entity) == BehaviorNodeType::MoveTo);
        }
    }
    CHECK(f.value(entity, "arrived").flag);
    CHECK(flatDistance(f.world.get<Transform>(entity).position, goal) < 0.6f);
    CHECK(f.running(entity) == BehaviorNodeType::Wait);
}

TEST_CASE("a moveTo without a NavAgent fails")
{
    AiFixture f;
    f.ground();
    f.write("game:/move.behavior", kMoveTree);
    const Entity entity = f.agent(Vec3(0.0f), "game:/move.behavior", false);
    f.world.get<BehaviorAgent>(entity).blackboard = {vector("goal", Vec3(5.0f, 0.0f, 0.0f))};
    f.ai.waitIdle();
    f.step(2);
    CHECK_FALSE(f.value(entity, "arrived").flag);
    CHECK(f.world.get<AiAgentState>(entity).status == BehaviorStatus::Failure);
}

TEST_CASE("wander sends the NavAgent to reachable navmesh points within its radius of its key")
{
    AiFixture f;
    f.ground();
    f.write("game:/wander.behavior", kWanderTree);
    const Vec3 home(2.0f, 0.0f, -3.0f);
    const Entity entity = f.agent(home, "game:/wander.behavior");
    f.world.get<BehaviorAgent>(entity).blackboard = {vector("home", home)};
    f.ai.waitIdle();
    std::array<Vec3, 64> destinations{};
    u32 count = 0;
    for (u32 i = 0; i < 900; ++i)
    {
        f.step();
        const std::optional<Vec3>& destination = f.world.get<NavAgent>(entity).destination;
        if (destination && (count == 0 || *destination != destinations[count - 1]) &&
            count < destinations.size())
        {
            destinations[count++] = *destination;
        }
    }
    CHECK(count >= 3);
    for (u32 i = 0; i < count; ++i)
    {
        CHECK(flatDistance(destinations[i], home) <= 4.0f + 1e-3f);
        CHECK(std::abs(destinations[i].y) < 0.3f);
    }
    CHECK(flatDistance(f.world.get<Transform>(entity).position, home) < 5.0f);
}

TEST_CASE(
    "a guard with LowerPriority interrupts wandering when perception sees a target, then the bite starts")
{
    AiFixture f;
    f.ground();
    f.write("game:/guard.behavior", kGuardTree);
    const Entity guard = f.agent(Vec3(0.0f), "game:/guard.behavior");
    f.world.get<BehaviorAgent>(guard).blackboard = {vector("home", Vec3(0.0f))};
    AiPerception perception;
    perception.sightAngle = 2.0f * std::numbers::pi_v<f32>;
    perception.targetKey = "target";
    f.world.set(guard, perception);
    const Entity intruder =
        f.world.create(test::at(Vec3(10.0f, 0.0f, 10.0f)),
                       AiStimulus{.team = 0, .offset = Vec3(0.0f, 1.0f, 0.0f), .visible = false});
    f.ai.waitIdle();
    f.step(60);
    const BehaviorNodeType idle = f.running(guard);
    CHECK((idle == BehaviorNodeType::Wander || idle == BehaviorNodeType::Wait));
    CHECK_FALSE(f.value(guard, "target").set);

    f.world.get<AiStimulus>(intruder).visible = true;
    f.step();
    CHECK(f.value(guard, "target").entity == intruder);
    CHECK(f.world.get<AiAgentState>(guard).target == intruder);
    CHECK(f.running(guard) == BehaviorNodeType::MoveTo);
    f.step();
    REQUIRE(f.world.get<NavAgent>(guard).destination.has_value());
    CHECK(flatDistance(*f.world.get<NavAgent>(guard).destination, Vec3(10.0f, 0.0f, 10.0f)) < 0.01f);

    u32 updates = 0;
    while (updates < 900 && f.countActions("bite", AiActionPhase::Started) == 0)
    {
        f.step();
        ++updates;
    }
    CHECK(f.countActions("bite", AiActionPhase::Started) == 1);
    CHECK(flatDistance(f.world.get<Transform>(guard).position, Vec3(10.0f, 0.0f, 10.0f)) < 1.6f);
}

TEST_CASE(
    "trees with a tickInterval tick once per interval with the time since their last tick, spread over it")
{
    AiFixture f;
    f.write("game:/wait.behavior", kWaitTree);
    std::array<Entity, 3> agents{};
    for (Entity& entity : agents)
    {
        entity = f.agent(Vec3(0.0f), "game:/wait.behavior", false);
        f.world.get<BehaviorAgent>(entity).tickInterval = 0.5f;
    }
    f.ai.waitIdle();
    // Steps of 1/8 s: four per interval, exact in binary.
    std::array<u32, 3> firstTick{};
    u32 total = 0;
    for (u32 update = 1; update <= 40; ++update)
    {
        f.ai.update(0.125f, &f.jobs, nullptr, true);
        total += f.ai.statistics().ticked;
        for (u32 i = 0; i < agents.size(); ++i)
        {
            if (firstTick[i] == 0 && f.state(agents[i]).ticks > 0)
            {
                firstTick[i] = update;
            }
        }
    }
    CHECK(firstTick[0] == 1);
    CHECK(firstTick[1] != firstTick[0]);
    u32 ticks = 0;
    for (const Entity entity : agents)
    {
        const BehaviorTreeState& state = f.state(entity);
        CHECK(state.ticks >= 10);
        CHECK(state.ticks <= 11);
        // Each tick advances the tree by the time since the previous one.
        CHECK(state.time <= 5.0 + 1e-6);
        CHECK(state.time > 4.4);
        ticks += static_cast<u32>(state.ticks);
    }
    CHECK(total == ticks);
}

TEST_CASE("Inactive and disabled agents keep their state and do not tick")
{
    AiFixture f;
    f.write("game:/wait.behavior", kWaitTree);
    const Entity entity = f.agent(Vec3(0.0f), "game:/wait.behavior", false);
    f.ai.waitIdle();
    f.step(3);
    CHECK(f.state(entity).ticks == 3);
    f.world.emplace<Inactive>(entity);
    f.step(3);
    CHECK(f.state(entity).ticks == 3);
    CHECK(f.world.get<AiAgentState>(entity).status == BehaviorStatus::Running);
    f.world.remove<Inactive>(entity);
    f.step();
    CHECK(f.state(entity).ticks == 4);
    f.world.get<BehaviorAgent>(entity).enabled = false;
    f.step(2);
    CHECK(f.state(entity).ticks == 4);

    // Without the update advancing, nothing ticks either.
    f.world.get<BehaviorAgent>(entity).enabled = true;
    f.ai.update(kStep, &f.jobs, nullptr, false);
    CHECK(f.state(entity).ticks == 4);
}

TEST_CASE("a changed tree file restarts its agents with the new tree; a broken one keeps the old")
{
    AiFixture f;
    const std::string_view path = "game:/reload.behavior";
    std::string text(kActionTree);
    f.write(path, text);
    const Entity entity = f.agent(Vec3(0.0f), path, false);
    f.ai.waitIdle();
    f.step();
    REQUIRE(f.countActions("bite", AiActionPhase::Started) == 1);
    const u32 first = f.ai.actions()[0].action;

    text.replace(text.find(R"("action": "bite")"), 16, R"("action": "chew")");
    f.write(path, text);
    CHECK(f.streamer.reload(path));
    f.ai.waitIdle();
    f.step();
    CHECK(f.countActions("bite", AiActionPhase::Aborted) == 1);
    CHECK(f.countActions("chew", AiActionPhase::Started) == 1);
    CHECK(f.ai.finishAction(entity, first, true).error() == AiError::UnknownAction);

    f.write(path, "{}");
    CHECK(f.streamer.reload(path));
    f.ai.waitIdle();
    f.step();
    CHECK(f.ai.actions().empty());
    const std::optional<u32> slot = f.ai.findSlot(entity);
    REQUIRE(slot.has_value());
    REQUIRE(f.ai.tree(*slot) != nullptr);
    CHECK(f.ai.tree(*slot)->nodes[2].action == "chew");
}

TEST_CASE("an Entity that loses its BehaviorAgent and AiPerception loses its slot and AiAgentState")
{
    AiFixture f;
    f.write("game:/wait.behavior", kWaitTree);
    const Entity entity = f.agent(Vec3(0.0f), "game:/wait.behavior", false);
    f.ai.waitIdle();
    REQUIRE(f.world.has<AiAgentState>(entity));
    f.world.remove<BehaviorAgent>(entity);
    f.step();
    CHECK_FALSE(f.world.has<AiAgentState>(entity));
    CHECK_FALSE(f.ai.findSlot(entity).has_value());
    CHECK(f.ai.statistics().agents == 0);
}

namespace
{

// A guarded wanderer world: 12 agents perceive 3 moving intruders behind 2 walls through physics, hear a
// noise every 50 updates and bite when they reach one (game code finishes the bites).
std::array<u64, 2> runGuards(u32 workers)
{
    AiFixture f(workers, AiWorldDesc{.maxAgents = 64,
                                     .maxSightRaysPerUpdate = 6,
                                     .maxSightCandidates = 4,
                                     .maxNoisesPerUpdate = 16,
                                     .maxActions = 64,
                                     .seed = 5});
    PhysicsWorld physics(f.world, f.jobs);
    buildGround(f.world);
    addBox(f.world, Vec3(-4.0f, 1.0f, 0.0f), Vec3(3.0f, 1.0f, 0.25f));
    addBox(f.world, Vec3(5.0f, 1.0f, 4.0f), Vec3(0.25f, 1.0f, 3.0f));
    f.navigation.waitIdle(&f.jobs);
    f.write("game:/guard.behavior", kGuardTree);
    std::array<Entity, 12> guards{};
    for (u32 i = 0; i < guards.size(); ++i)
    {
        const Vec3 home(static_cast<f32>(i % 4) * 4.0f - 6.0f, 0.0f, static_cast<f32>(i / 4) * 5.0f - 8.0f);
        guards[i] = f.agent(home, "game:/guard.behavior");
        f.world.get<BehaviorAgent>(guards[i]).blackboard = {vector("home", home)};
        f.world.get<BehaviorAgent>(guards[i]).tickInterval = i % 3 == 0 ? 0.1f : 0.0f;
        AiPerception perception;
        perception.sightRadius = 10.0f;
        perception.targetKey = "target";
        perception.memorySeconds = 1.0f;
        f.world.set(guards[i], perception);
        f.world.set(guards[i], EntityGuid{.value = 0x100u + i});
    }
    std::array<Entity, 3> intruders{};
    for (Entity& intruder : intruders)
    {
        intruder = f.world.create(test::at(Vec3(0.0f)),
                                  AiStimulus{.team = 0, .offset = Vec3(0.0f, 1.0f, 0.0f), .visible = true});
    }
    f.ai.waitIdle();
    u64 actions = 0;
    for (u32 step = 0; step < 600; ++step)
    {
        const f32 time = static_cast<f32>(step) * kStep;
        for (u32 k = 0; k < intruders.size(); ++k)
        {
            const f32 angle = time * 0.4f + static_cast<f32>(k) * 2.1f;
            f.world.get<Transform>(intruders[k]).position =
                Vec3(14.0f * std::cos(angle), 0.0f, 14.0f * std::sin(angle));
        }
        if (step % 50 == 0)
        {
            REQUIRE(f.ai.reportNoise({.position = f.world.get<Transform>(intruders[0]).position,
                                      .radius = 12.0f,
                                      .instigator = intruders[0],
                                      .team = 0})
                        .hasValue());
        }
        physics.update(static_cast<f64>(kStep));
        f.ai.update(kStep, &f.jobs, &physics, true);
        for (const AiActionRequest& request : f.ai.actions())
        {
            ++actions;
            if (request.phase == AiActionPhase::Started)
            {
                REQUIRE(f.ai.finishAction(request.entity, request.action, true).hasValue());
            }
        }
        f.navigation.update(kStep, &f.jobs, true);
    }
    u64 hash = 1469598103934665603ull;
    const auto mix = [&hash](u32 bits) {
        hash = (hash ^ bits) * 1099511628211ull;
    };
    for (const Entity guard : guards)
    {
        const Transform& transform = f.world.get<Transform>(guard);
        for (const f32 value : {transform.position.x, transform.position.y, transform.position.z})
        {
            u32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            mix(bits);
        }
        const AiAgentState& state = f.world.get<AiAgentState>(guard);
        mix(static_cast<u32>(state.runningNode));
        mix(static_cast<u32>(state.status));
        mix(state.target.index);
    }
    return {hash, actions};
}

} // namespace

TEST_CASE("the same guarded World after 600 fixed steps is identical with 1 and 4 workers")
{
    const std::array<u64, 2> one = runGuards(1);
    const std::array<u64, 2> four = runGuards(4);
    CHECK(one[1] > 0);
    CHECK(four[0] == one[0]);
    CHECK(four[1] == one[1]);
}

TEST_CASE("a steady AI update with perception, trees, moves and actions does not allocate")
{
    AiFixture f(4);
    PhysicsWorld physics(f.world, f.jobs);
    buildGround(f.world);
    addBox(f.world, Vec3(0.0f, 1.0f, 0.0f), Vec3(3.0f, 1.0f, 0.25f));
    f.navigation.waitIdle(&f.jobs);
    f.write("game:/guard.behavior", kGuardTree);
    for (u32 i = 0; i < 40; ++i)
    {
        const Vec3 home(static_cast<f32>(i % 8) * 4.0f - 14.0f, 0.0f, static_cast<f32>(i / 8) * 6.0f - 12.0f);
        const Entity guard = f.agent(home, "game:/guard.behavior");
        f.world.get<BehaviorAgent>(guard).blackboard = {vector("home", home)};
        AiPerception perception;
        perception.targetKey = "target";
        perception.memorySeconds = 0.5f;
        f.world.set(guard, perception);
    }
    std::array<Entity, 10> intruders{};
    for (Entity& intruder : intruders)
    {
        intruder = f.world.create(test::at(Vec3(0.0f)),
                                  AiStimulus{.team = 0, .offset = Vec3(0.0f, 1.0f, 0.0f), .visible = true});
    }
    f.ai.waitIdle();
    u64 allocations = 0;
    u32 ticked = 0;
    u32 rays = 0;
    for (u32 step = 0; step < 240; ++step)
    {
        const f32 time = static_cast<f32>(step) * kStep;
        for (u32 k = 0; k < intruders.size(); ++k)
        {
            const f32 angle = time * 0.5f + static_cast<f32>(k) * 0.63f;
            f.world.get<Transform>(intruders[k]).position =
                Vec3(12.0f * std::cos(angle), 0.0f, 12.0f * std::sin(angle));
        }
        physics.update(static_cast<f64>(kStep));
        {
            test::ScopedAllocationCounter counter;
            static_cast<void>(f.ai.reportNoise({.position = Vec3(0.0f),
                                                .radius = 5.0f,
                                                .instigator = intruders[step % intruders.size()],
                                                .team = 0}));
            f.ai.update(kStep, &f.jobs, &physics, true);
            for (const AiActionRequest& request : f.ai.actions())
            {
                if (request.phase == AiActionPhase::Started)
                {
                    static_cast<void>(f.ai.finishAction(request.entity, request.action, true));
                }
            }
            if (step >= 120)
            {
                allocations += counter.count();
                ticked += f.ai.statistics().ticked;
                rays += f.ai.statistics().rays;
            }
        }
        f.navigation.update(kStep, &f.jobs, true);
    }
    CHECK(allocations == 0);
    CHECK(ticked == 40u * 120u);
    CHECK(rays > 0);
}

namespace
{

// A started-later session with fixed steps over a project with the game mount, for the AI step.
struct SessionFixture
{
    SessionFixture()
    {
        reflectAll();
        REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
        REQUIRE(files.writeText("assets:/move.behavior", kMoveTree) == FileError::None);
        GameSessionDesc desc;
        desc.jobs = &jobs;
        desc.files = &files;
        desc.streamer = &streamer;
        desc.audioOutput = AudioOutput::Offline;
        desc.fixedStep = true;
        auto created = GameSession::create(desc);
        REQUIRE(created.hasValue());
        session = std::move(*created);
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .queueCapacity = 1024}};
    AssetStreamer streamer{files, jobs};
    std::unique_ptr<GameSession> session;
};

} // namespace

TEST_CASE("the GameSession's AI step ticks trees only while started and their moves drive the NavAgents")
{
    SessionFixture fixture;
    World& world = fixture.session->world();
    CHECK(&fixture.session->ai().world() == &world);
    buildGround(world);
    BehaviorAgent behavior;
    behavior.tree = AssetRef("assets:/move.behavior");
    const Vec3 goal(5.0f, 0.0f, 4.0f);
    behavior.blackboard = {vector("goal", goal)};
    NavAgent nav;
    nav.drive = NavDrive::Transform;
    const Entity entity = world.create(test::at(Vec3(-5.0f, 0.0f, -4.0f)), behavior, nav);
    for (u32 i = 0; i < 5; ++i)
    {
        fixture.session->update(0.0);
    }
    REQUIRE(world.has<AiAgentState>(entity));
    CHECK_FALSE(world.get<NavAgent>(entity).destination.has_value());
    const std::optional<u32> slot = fixture.session->ai().findSlot(entity);
    REQUIRE(slot.has_value());
    REQUIRE(fixture.session->ai().treeState(*slot) != nullptr);
    CHECK(fixture.session->ai().treeState(*slot)->ticks == 0);

    fixture.session->start();
    for (u32 i = 0;
         i < 600 && !fixture.session->ai().value(entity, "arrived").valueOr(BlackboardValue{}).flag; ++i)
    {
        fixture.session->update(0.0);
    }
    CHECK(fixture.session->ai().value(entity, "arrived").valueOr(BlackboardValue{}).flag);
    CHECK(flatDistance(world.get<Transform>(entity).position, goal) < 0.6f);
}
