#include "Budget.hpp"

#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorld.hpp>
#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/ai/BehaviorTreeState.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <string_view>
#include <system_error>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kTrees = 1000;

// 30 nodes: a chase that preempts, a rest when tired, and a wander with bookkeeping in parallel.
constexpr std::string_view kTree = R"({"format": "illusion.behavior", "version": 1,
  "blackboard": [{"name": "target", "type": "Entity"}, {"name": "home", "type": "Vector3", "default": [0, 0, 0]},
                 {"name": "count", "type": "Int"}, {"name": "speed", "type": "Float"},
                 {"name": "flag", "type": "Bool"}],
  "root": {"selector": [
    {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority", "child":
      {"timeLimit": {"seconds": 5, "child": {"sequence": [
        {"moveTo": {"key": "target", "acceptance": 1.5}}, {"action": "bite"}, {"wait": {"seconds": 0.5}}]}}}}},
    {"guard": {"key": "count", "compare": "GreaterEqual", "value": 3, "abort": "Self", "child":
      {"sequence": [{"setValue": {"key": "count", "value": 0}}, {"action": "rest"},
                    {"wait": {"seconds": 1, "deviation": 0.5}}]}}},
    {"parallel": {"successes": 1, "children": [
      {"repeat": {"child": {"setValue": {"key": "speed", "value": 1}}}},
      {"sequence": [
        {"wander": {"key": "home", "radius": 8}}, {"wait": {"seconds": 2, "deviation": 1}},
        {"inverter": {"child": {"condition": {"key": "count", "compare": "Less", "value": 0}}}},
        {"succeeder": {"child": {"stop": {}}}},
        {"cooldown": {"seconds": 1, "child": {"retry": {"count": 2, "child": {"action": "sniff"}}}}},
        {"setValue": {"key": "count", "value": 4}},
        {"condition": {"key": "speed", "compare": "Greater", "value": 0}},
        {"succeeder": {"child": {"failer": {"child": {"condition": {"key": "flag", "compare": "Equal",
                                                                    "value": true}}}}}}]}]}}]}})";

// Moves arrive and actions succeed a few ticks after they start.
class BenchContext final : public BehaviorContext
{
public:
    u32 now = 0;
    Vec3 self{0.0f};

    [[nodiscard]] Vec3 selfPosition() const override { return self; }
    [[nodiscard]] std::optional<Vec3> position(Entity entity) const override
    {
        return Vec3(static_cast<f32>(entity.index % 16), 0.0f, 4.0f);
    }
    [[nodiscard]] std::optional<Vec3> randomPoint(const Vec3& center, f32 radius, Random& random) override
    {
        return center + Vec3(random.uniformF32() * radius, 0.0f, random.uniformF32() * radius);
    }
    u32 requestMove(const Vec3&, f32) override
    {
        m_moveStart = now;
        return ++m_moves;
    }
    [[nodiscard]] BehaviorStatus moveStatus(u32 handle) const override
    {
        if (handle != m_moves)
        {
            return BehaviorStatus::Failure;
        }
        return now - m_moveStart > 6 ? BehaviorStatus::Success : BehaviorStatus::Running;
    }
    void stopMoving() override { m_moveStart = now; }
    u32 startAction(u32, std::string_view) override
    {
        m_actionStart = now;
        return ++m_actions;
    }
    [[nodiscard]] BehaviorStatus actionStatus(u32 handle) const override
    {
        if (handle != m_actions)
        {
            return BehaviorStatus::Failure;
        }
        return now - m_actionStart > 3 ? BehaviorStatus::Success : BehaviorStatus::Running;
    }
    void abortAction(u32) override {}

private:
    u32 m_moves = 0;
    u32 m_moveStart = 0;
    u32 m_actions = 0;
    u32 m_actionStart = 0;
};

// kTree as "bench:/bench.behavior" in a temporary directory.
struct TreeFiles
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / std::format("illusion-bench-ai-{}", std::random_device{}());
    VirtualFileSystem files;
    bool ready = false;

    TreeFiles()
    {
        std::filesystem::create_directories(root);
        ready = files.mount("bench", root, MountAccess::ReadWrite) == FileError::None &&
                files.writeText("bench:/bench.behavior", kTree) == FileError::None;
    }
    ~TreeFiles()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    TreeFiles(const TreeFiles&) = delete;
    TreeFiles& operator=(const TreeFiles&) = delete;
};

Transform placed(Vec3 position, f32 yaw = 0.0f)
{
    Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    return transform;
}

} // namespace

// Budget: ns per tick of 1,000 trees of 30 nodes on one thread, about 8x a desktop result (33 us); ADR 0062
// aims at 1 ms.
ILL_BENCHMARK("ai/tick-1000", 250'000.0)
{
    const Expected<BehaviorTree, std::string> tree = parseBehaviorTree(kTree);
    ILL_CHECK(tree.hasValue() && tree->nodes.size() == 30, "the benchmark tree has 30 nodes");
    std::vector<BehaviorTreeState> states;
    std::vector<BenchContext> contexts(kTrees);
    states.reserve(kTrees);
    for (u32 i = 0; i < kTrees; ++i)
    {
        states.push_back(makeBehaviorTreeState(*tree, i));
        contexts[i].self = Vec3(static_cast<f32>(i % 32), 0.0f, static_cast<f32>(i / 32));
    }
    u32 tick = 0;
    bench.unit("1,000 trees").run("tick 1,000 trees of 30 nodes", [&] {
        ++tick;
        u32 running = 0;
        for (u32 i = 0; i < kTrees; ++i)
        {
            BehaviorTreeState& state = states[i];
            BlackboardValue& target = state.blackboard[0];
            target.set = (tick + i) % 97 < 30;
            target.entity = Entity{.index = i, .generation = 0};
            contexts[i].now = tick;
            running += tickBehaviorTree(*tree, state, 1.0f / 30.0f, contexts[i]) == BehaviorStatus::Running
                           ? 1u
                           : 0u;
        }
        ankerl::nanobench::doNotOptimizeAway(running);
    });
}

// Budget: ns per AiWorld update of 200 perceivers among 50 moving stimuli on a 100 m ground with 40 walls,
// 256 line-of-sight raycasts, with the Job System, about 7x a 12-thread desktop result (0.1 ms); ADR 0062
// aims at 1 ms.
ILL_BENCHMARK("ai/perception-200", 750'000.0)
{
    constexpr f32 kStep = 1.0f / 60.0f;
    JobSystem jobs;
    World world;
    PhysicsWorld physics(world, jobs);
    NavWorld navigation(world, nullptr);
    AiWorld ai(world, navigation, nullptr,
               AiWorldDesc{.maxAgents = 256,
                           .maxSightRaysPerUpdate = 256,
                           .maxSightCandidates = 8,
                           .maxNoisesPerUpdate = 64,
                           .maxActions = 64,
                           .seed = 1});
    RigidBody fixed;
    fixed.motionType = MotionType::Static;
    Collider ground;
    ground.shape = ColliderShape::Box;
    ground.halfExtents = Vec3(50.0f, 0.5f, 50.0f);
    world.create(placed(Vec3(0.0f, -0.5f, 0.0f)), fixed, ground);
    Random random(3);
    const auto point = [&random] {
        return Vec3(random.uniformF32() * 90.0f - 45.0f, 0.0f, random.uniformF32() * 90.0f - 45.0f);
    };
    for (u32 i = 0; i < 40; ++i)
    {
        Collider wall;
        wall.shape = ColliderShape::Box;
        wall.halfExtents = i % 2 == 0 ? Vec3(4.0f, 1.5f, 0.25f) : Vec3(0.25f, 1.5f, 4.0f);
        world.create(placed(point() + Vec3(0.0f, 1.5f, 0.0f)), fixed, wall);
    }
    for (u32 i = 0; i < 200; ++i)
    {
        AiPerception perception;
        perception.sightRadius = 20.0f;
        world.create(placed(point(), random.uniformF32() * 6.2831853f), perception);
    }
    std::vector<Entity> stimuli;
    std::vector<Vec3> velocities;
    for (u32 i = 0; i < 50; ++i)
    {
        stimuli.push_back(world.create(
            placed(point()), AiStimulus{.team = 0, .offset = Vec3(0.0f, 1.0f, 0.0f), .visible = true}));
        velocities.push_back(
            Vec3(random.uniformF32() * 4.0f - 2.0f, 0.0f, random.uniformF32() * 4.0f - 2.0f));
    }
    physics.update(static_cast<f64>(kStep));
    const auto update = [&] {
        for (usize i = 0; i < stimuli.size(); ++i)
        {
            Vec3& position = world.get<Transform>(stimuli[i]).position;
            position += velocities[i] * kStep;
            if (std::abs(position.x) > 45.0f || std::abs(position.z) > 45.0f)
            {
                velocities[i] = -velocities[i];
            }
        }
        ai.update(kStep, &jobs, &physics, true);
    };
    for (u32 i = 0; i < 30; ++i)
    {
        update();
    }
    ILL_CHECK(ai.statistics().rays == 256, "the perceivers have more candidates than the ray budget");
    bench.unit("update").run("perceive with 200 perceivers, 50 stimuli and 256 rays", [&] {
        update();
        ankerl::nanobench::doNotOptimizeAway(ai.statistics().rays);
    });
}

// Budget: ns per AiWorld update of 1,000 agents running the 30-node tree (no navigation, so moves fail at
// once; every started action is finished), with the Job System, about 7x a 12-thread desktop result (0.18
// ms).
ILL_BENCHMARK("ai/world-1000", 1'300'000.0)
{
    constexpr f32 kStep = 1.0f / 60.0f;
    TreeFiles tree;
    ILL_CHECK(tree.ready, "the benchmark tree is written");
    JobSystem jobs;
    AssetStreamer streamer(tree.files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    NavWorld navigation(world, nullptr);
    AiWorld ai(world, navigation, &streamer);
    for (u32 i = 0; i < kTrees; ++i)
    {
        BehaviorAgent agent;
        agent.tree = AssetRef("bench:/bench.behavior");
        world.create(placed(Vec3(static_cast<f32>(i % 32), 0.0f, static_cast<f32>(i / 32))), agent);
    }
    ai.waitIdle();
    ILL_CHECK(ai.statistics().agents == kTrees && ai.statistics().pending == 0, "every agent has its tree");
    const auto update = [&] {
        ai.update(kStep, &jobs, nullptr, true);
        for (const AiActionRequest& request : ai.actions())
        {
            if (request.phase == AiActionPhase::Started)
            {
                static_cast<void>(ai.finishAction(request.entity, request.action, true));
            }
        }
    };
    for (u32 i = 0; i < 30; ++i)
    {
        update();
    }
    bench.unit("update").run("update 1,000 agents", [&] {
        update();
        ankerl::nanobench::doNotOptimizeAway(ai.statistics().ticked);
    });
}
