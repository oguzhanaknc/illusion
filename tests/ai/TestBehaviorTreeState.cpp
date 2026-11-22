#include "TestSupport.hpp"

#include <illusion/ai/BehaviorTreeState.hpp>

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr std::string_view kKeys = R"([{"name": "flag", "type": "Bool"}, {"name": "count", "type": "Int"},
    {"name": "speed", "type": "Float"}, {"name": "home", "type": "Vector3"}, {"name": "target", "type": "Entity"}])";

constexpr i32 kFlag = 0;
constexpr i32 kCount = 1;
constexpr i32 kHome = 3;
constexpr i32 kTarget = 4;

// Records what a tree asks for; moves and actions finish when a test says so.
class FakeContext final : public BehaviorContext
{
public:
    Vec3 self{0.0f};
    std::array<std::optional<Vec3>, 4> entities{};
    bool noPoint = false;
    Vec3 lastCenter{0.0f};
    u32 moves = 0;
    Vec3 moveTarget{0.0f};
    f32 moveAcceptance = 0.0f;
    BehaviorStatus move = BehaviorStatus::Idle;
    u32 stops = 0;
    u32 actions = 0;
    std::array<BehaviorStatus, 32> results{};
    std::array<u32, 32> actionNodes{};
    std::array<std::string_view, 32> actionNames{};
    u32 aborts = 0;
    std::array<u32, 32> aborted{};

    // Slot of the action or abort @p count in the arrays above, which wrap around.
    [[nodiscard]] static usize slot(u32 count) noexcept { return count % 32; }

    [[nodiscard]] Vec3 selfPosition() const override { return self; }

    [[nodiscard]] std::optional<Vec3> position(Entity entity) const override
    {
        return entity.index < entities.size() ? entities.at(entity.index) : std::nullopt;
    }

    [[nodiscard]] std::optional<Vec3> randomPoint(const Vec3& center, f32 radius, Random& random) override
    {
        if (noPoint)
        {
            return std::nullopt;
        }
        lastCenter = center;
        return center + Vec3(random.uniformF32() * radius, 0.0f, 0.0f);
    }

    u32 requestMove(const Vec3& target, f32 acceptance) override
    {
        moveTarget = target;
        moveAcceptance = acceptance;
        move = BehaviorStatus::Running;
        return ++moves;
    }

    [[nodiscard]] BehaviorStatus moveStatus(u32 handle) const override
    {
        return handle == moves ? move : BehaviorStatus::Failure;
    }

    void stopMoving() override
    {
        ++stops;
        if (move == BehaviorStatus::Running)
        {
            move = BehaviorStatus::Failure;
        }
    }

    u32 startAction(u32 node, std::string_view name) override
    {
        results.at(slot(actions)) = BehaviorStatus::Running;
        actionNodes.at(slot(actions)) = node;
        actionNames.at(slot(actions)) = name;
        return ++actions;
    }

    [[nodiscard]] BehaviorStatus actionStatus(u32 handle) const override
    {
        return results.at(slot(handle - 1));
    }

    void abortAction(u32 handle) override { aborted.at(slot(aborts++)) = handle; }

    // Finishes the action of @p handle.
    void finish(u32 handle, bool success)
    {
        results.at(slot(handle - 1)) = success ? BehaviorStatus::Success : BehaviorStatus::Failure;
    }
};

BehaviorTree parse(std::string_view root, std::string_view keys = kKeys)
{
    std::string text = R"({"format": "illusion.behavior", "version": 1, "blackboard": )";
    text += keys;
    text += R"(, "root": )";
    text += root;
    text += "}";
    Expected<BehaviorTree, std::string> tree = parseBehaviorTree(text);
    REQUIRE_MESSAGE(tree.hasValue(), (tree ? std::string() : tree.error()));
    return std::move(*tree);
}

struct Agent
{
    explicit Agent(std::string_view root, u64 seed = 1)
        : tree(parse(root))
        , state(makeBehaviorTreeState(tree, seed))
    {
    }

    BehaviorStatus tick(f32 seconds = 1.0f) { return tickBehaviorTree(tree, state, seconds, context); }

    void setTarget(u32 index)
    {
        BlackboardValue& value = state.blackboard[kTarget];
        value.set = true;
        value.entity = Entity{.index = index, .generation = 0};
    }

    BehaviorTree tree;
    BehaviorTreeState state;
    FakeContext context;
};

} // namespace

TEST_CASE("a sequence runs its children in order until one fails")
{
    Agent passing(
        R"({"sequence": [{"setValue": {"key": "count", "value": 1}}, {"action": "a"}, {"action": "b"}]})");
    CHECK(passing.tick() == BehaviorStatus::Running);
    CHECK(passing.state.blackboard[kCount].integer == 1);
    REQUIRE(passing.context.actions == 1);
    CHECK(passing.context.actionNames[0] == "a");
    CHECK(passing.context.actionNodes[0] == 2);
    passing.context.finish(1, true);
    CHECK(passing.tick() == BehaviorStatus::Running);
    REQUIRE(passing.context.actions == 2);
    CHECK(passing.context.actionNames[1] == "b");
    passing.context.finish(2, true);
    CHECK(passing.tick() == BehaviorStatus::Success);

    Agent failing(R"({"sequence": [{"setValue": {"key": "count", "value": 1}},
        {"condition": {"key": "count", "compare": "Equal", "value": 2}}, {"setValue": {"key": "flag", "value": true}}]})");
    CHECK(failing.tick() == BehaviorStatus::Failure);
    CHECK(failing.state.blackboard[kCount].integer == 1);
    CHECK_FALSE(failing.state.blackboard[kFlag].flag);
}

TEST_CASE("a selector runs its children in order until one succeeds")
{
    Agent agent(R"({"selector": [{"condition": {"key": "flag", "compare": "Equal", "value": true}},
        {"action": "x"}, {"action": "y"}, {"action": "z"}]})");
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 1);
    CHECK(agent.context.actionNames[0] == "x");
    agent.context.finish(1, false);
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 2);
    CHECK(agent.context.actionNames[1] == "y");
    agent.context.finish(2, true);
    CHECK(agent.tick() == BehaviorStatus::Success);
    CHECK(agent.context.actions == 2);

    agent.state.blackboard[kFlag].flag = true;
    CHECK(agent.tick() == BehaviorStatus::Success);
    CHECK(agent.context.actions == 2);

    Agent none(R"({"selector": [{"condition": {"key": "flag", "compare": "Equal", "value": true}},
        {"condition": {"key": "count", "compare": "Greater", "value": 0}}]})");
    CHECK(none.tick() == BehaviorStatus::Failure);
}

TEST_CASE("a parallel succeeds once enough children succeed and aborts the rest")
{
    Agent one(R"({"parallel": {"successes": 1, "children": [{"action": "a"}, {"action": "b"}]}})");
    CHECK(one.tick() == BehaviorStatus::Running);
    CHECK(one.context.actions == 2);
    one.context.finish(1, true);
    CHECK(one.tick() == BehaviorStatus::Success);
    REQUIRE(one.context.aborts == 1);
    CHECK(one.context.aborted[0] == 2);
    CHECK(runningNode(one.tree, one.state) == -1);

    Agent all(R"({"parallel": {"children": [{"action": "a"}, {"action": "b"}]}})");
    CHECK(all.tick() == BehaviorStatus::Running);
    all.context.finish(1, true);
    CHECK(all.tick() == BehaviorStatus::Running);
    CHECK(all.context.actions == 2);
    all.context.finish(2, true);
    CHECK(all.tick() == BehaviorStatus::Success);
    CHECK(all.context.aborts == 0);
    CHECK(all.tick() == BehaviorStatus::Running);
    CHECK(all.context.actions == 4);

    Agent two(
        R"({"parallel": {"successes": 2, "children": [{"action": "a"}, {"action": "b"}, {"action": "c"}]}})");
    CHECK(two.tick() == BehaviorStatus::Running);
    two.context.finish(1, false);
    CHECK(two.tick() == BehaviorStatus::Running);
    two.context.finish(2, false);
    CHECK(two.tick() == BehaviorStatus::Failure);
    REQUIRE(two.context.aborts == 1);
    CHECK(two.context.aborted[0] == 3);
}

TEST_CASE("inverter, succeeder and failer change how their child ends")
{
    constexpr std::string_view kFails =
        R"({"condition": {"key": "flag", "compare": "Equal", "value": true}})";
    Agent inverter(std::string(R"({"inverter": {"child": )") + std::string(kFails) + "}}");
    CHECK(inverter.tick() == BehaviorStatus::Success);
    inverter.state.blackboard[kFlag].flag = true;
    CHECK(inverter.tick() == BehaviorStatus::Failure);

    Agent succeeder(std::string(R"({"succeeder": {"child": )") + std::string(kFails) + "}}");
    CHECK(succeeder.tick() == BehaviorStatus::Success);

    Agent failer(R"({"failer": {"child": {"stop": {}}}})");
    CHECK(failer.tick() == BehaviorStatus::Failure);

    Agent running(R"({"inverter": {"child": {"action": "a"}}})");
    CHECK(running.tick() == BehaviorStatus::Running);
    running.context.finish(1, true);
    CHECK(running.tick() == BehaviorStatus::Failure);
}

TEST_CASE("repeat runs its child the given number of times, one run per tick")
{
    Agent counted(R"({"repeat": {"count": 3, "child": {"stop": {}}}})");
    CHECK(counted.tick() == BehaviorStatus::Running);
    CHECK(counted.tick() == BehaviorStatus::Running);
    CHECK(counted.tick() == BehaviorStatus::Success);
    CHECK(counted.context.stops == 3);

    Agent forever(R"({"repeat": {"child": {"stop": {}}}})");
    for (int i = 0; i < 10; ++i)
    {
        CHECK(forever.tick() == BehaviorStatus::Running);
    }
    CHECK(forever.context.stops == 10);

    Agent failing(R"({"repeat": {"count": 5, "child": {"action": "a"}}})");
    CHECK(failing.tick() == BehaviorStatus::Running);
    failing.context.finish(1, false);
    CHECK(failing.tick() == BehaviorStatus::Failure);
}

TEST_CASE("retry runs its child again after a failure up to the given number of times")
{
    Agent agent(
        R"({"retry": {"count": 2, "child": {"condition": {"key": "flag", "compare": "Equal", "value": true}}}})");
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.tick() == BehaviorStatus::Failure);

    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.state.blackboard[kFlag].flag = true;
    CHECK(agent.tick() == BehaviorStatus::Success);
}

TEST_CASE("cooldown fails for its seconds after its child ended")
{
    Agent agent(R"({"cooldown": {"seconds": 2, "child": {"stop": {}}}})");
    CHECK(agent.tick() == BehaviorStatus::Success);
    CHECK(agent.tick() == BehaviorStatus::Failure);
    CHECK(agent.tick() == BehaviorStatus::Success);
    CHECK(agent.context.stops == 2);
    CHECK(agent.tick(0.5f) == BehaviorStatus::Failure);
    CHECK(agent.tick(1.5f) == BehaviorStatus::Success);
}

TEST_CASE("timeLimit fails and aborts its child after its seconds")
{
    Agent agent(R"({"timeLimit": {"seconds": 1.5, "child": {"action": "a"}}})");
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.aborts == 0);
    CHECK(agent.tick() == BehaviorStatus::Failure);
    REQUIRE(agent.context.aborts == 1);
    CHECK(agent.context.aborted[0] == 1);

    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.context.finish(2, true);
    CHECK(agent.tick(0.25f) == BehaviorStatus::Success);

    Agent move(R"({"timeLimit": {"seconds": 1, "child": {"moveTo": {"key": "home"}}}})");
    move.state.blackboard[kHome].set = true;
    move.state.blackboard[kHome].vector = Vec3(10.0f, 0.0f, 0.0f);
    CHECK(move.tick(0.5f) == BehaviorStatus::Running);
    CHECK(move.context.moves == 1);
    CHECK(move.tick(0.5f) == BehaviorStatus::Failure);
    CHECK(move.context.stops == 1);
    CHECK(move.context.move == BehaviorStatus::Failure);
}

TEST_CASE("a guard without an abort mode checks its condition only when it starts")
{
    Agent agent(R"({"guard": {"key": "flag", "compare": "Equal", "value": true, "child": {"action": "a"}}})");
    CHECK(agent.tick() == BehaviorStatus::Failure);
    CHECK(agent.context.actions == 0);
    agent.state.blackboard[kFlag].flag = true;
    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.state.blackboard[kFlag].flag = false;
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.aborts == 0);
    agent.context.finish(1, true);
    CHECK(agent.tick() == BehaviorStatus::Success);
}

TEST_CASE("a Self guard aborts its running child when its condition turns false")
{
    Agent agent(R"({"guard": {"key": "flag", "compare": "Equal", "value": true, "abort": "Self",
        "child": {"action": "a"}}})");
    agent.state.blackboard[kFlag].flag = true;
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.state.blackboard[kFlag].flag = false;
    CHECK(agent.tick() == BehaviorStatus::Failure);
    REQUIRE(agent.context.aborts == 1);
    CHECK(agent.context.aborted[0] == 1);
    CHECK(runningNode(agent.tree, agent.state) == -1);
}

TEST_CASE("a LowerPriority guard aborts a later sibling when its condition turns true")
{
    Agent agent(R"({"selector": [
        {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority", "child": {"action": "chase"}}},
        {"action": "idle"}]})");
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 1);
    CHECK(agent.context.actionNames[0] == "idle");
    CHECK(runningNode(agent.tree, agent.state) == 3);

    agent.setTarget(1);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.actions == 1);

    agent.context.entities[1] = Vec3(3.0f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 2);
    CHECK(agent.context.actionNames[1] == "chase");
    REQUIRE(agent.context.aborts == 1);
    CHECK(agent.context.aborted[0] == 1);
    CHECK(runningNode(agent.tree, agent.state) == 2);

    agent.context.finish(2, false);
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 3);
    CHECK(agent.context.actionNames[2] == "idle");
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.actions == 3);

    agent.context.entities[1] = std::nullopt;
    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.context.entities[1] = Vec3(4.0f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 4);
    CHECK(agent.context.actionNames[3] == "chase");
    CHECK(agent.context.aborts == 2);
}

TEST_CASE("a LowerPriority guard also aborts its own child when its condition turns false")
{
    Agent agent(R"({"selector": [
        {"guard": {"key": "flag", "compare": "Equal", "value": true, "abort": "LowerPriority",
                   "child": {"action": "chase"}}},
        {"action": "idle"}]})");
    agent.state.blackboard[kFlag].flag = true;
    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.state.blackboard[kFlag].flag = false;
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.aborts == 1);
    CHECK(agent.context.aborted[0] == 1);
    REQUIRE(agent.context.actions == 2);
    CHECK(agent.context.actionNames[1] == "idle");
}

TEST_CASE("wait runs for its seconds with a deviation drawn from the seed")
{
    Agent plain(R"({"wait": {"seconds": 2}})");
    CHECK(plain.tick(0.5f) == BehaviorStatus::Running);
    CHECK(plain.tick(1.0f) == BehaviorStatus::Running);
    CHECK(plain.tick(0.5f) == BehaviorStatus::Success);

    Agent instant(R"({"wait": {"seconds": 0}})");
    CHECK(instant.tick(0.0f) == BehaviorStatus::Success);

    constexpr std::string_view kRoot = R"({"wait": {"seconds": 2, "deviation": 1}})";
    std::array<f32, 16> durations{};
    for (u64 seed = 0; seed < durations.size(); ++seed)
    {
        Agent agent(kRoot, seed);
        CHECK(agent.tick(0.0f) == BehaviorStatus::Running);
        durations.at(seed) = agent.state.nodes[0].timer;
        CHECK(durations.at(seed) >= 1.0f);
        CHECK(durations.at(seed) <= 3.0f);

        Agent again(kRoot, seed);
        CHECK(again.tick(0.0f) == BehaviorStatus::Running);
        CHECK(again.state.nodes[0].timer == durations.at(seed));
    }
    CHECK(durations[0] != durations[1]);
    CHECK(durations[1] != durations[2]);
}

TEST_CASE("moveTo moves to a Vector3 key and succeeds when the move arrives")
{
    Agent agent(R"({"moveTo": {"key": "home", "acceptance": 2}})");
    CHECK(agent.tick() == BehaviorStatus::Failure);
    CHECK(agent.context.moves == 0);

    agent.state.blackboard[kHome].set = true;
    agent.state.blackboard[kHome].vector = Vec3(10.0f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.moves == 1);
    CHECK(agent.context.moveTarget == Vec3(10.0f, 0.0f, 0.0f));
    CHECK(agent.context.moveAcceptance == 2.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.moves == 1);
    agent.context.move = BehaviorStatus::Success;
    CHECK(agent.tick() == BehaviorStatus::Success);

    agent.context.self = Vec3(9.0f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Success);
    CHECK(agent.context.moves == 1);

    agent.context.self = Vec3(0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    agent.context.move = BehaviorStatus::Failure;
    CHECK(agent.tick() == BehaviorStatus::Failure);
}

TEST_CASE("moveTo follows an Entity key and fails when it is gone")
{
    Agent agent(R"({"moveTo": {"key": "target", "acceptance": 1}})");
    agent.setTarget(2);
    CHECK(agent.tick() == BehaviorStatus::Failure);

    agent.context.entities[2] = Vec3(5.0f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.moves == 1);
    agent.context.entities[2] = Vec3(5.5f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.moves == 1);
    agent.context.entities[2] = Vec3(7.0f, 0.0f, 0.0f);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.moves == 2);
    CHECK(agent.context.moveTarget == Vec3(7.0f, 0.0f, 0.0f));

    agent.context.entities[2] = std::nullopt;
    CHECK(agent.tick() == BehaviorStatus::Failure);
    CHECK(agent.context.stops == 1);
}

TEST_CASE("wander moves to a random point around its key or the start point")
{
    Agent around(R"({"wander": {"radius": 6}})", 9);
    around.context.self = Vec3(1.0f, 2.0f, 3.0f);
    CHECK(around.tick() == BehaviorStatus::Running);
    CHECK(around.context.lastCenter == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(around.context.moveAcceptance == kWanderAcceptance);
    CHECK(around.state.nodes[0].target == around.context.moveTarget);
    const Vec3 first = around.context.moveTarget;
    around.context.move = BehaviorStatus::Success;
    around.context.self = Vec3(20.0f);
    CHECK(around.tick() == BehaviorStatus::Success);
    CHECK(around.tick() == BehaviorStatus::Running);
    CHECK(around.context.lastCenter == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(around.context.moveTarget != first);

    Agent same(R"({"wander": {"radius": 6}})", 9);
    same.context.self = Vec3(1.0f, 2.0f, 3.0f);
    CHECK(same.tick() == BehaviorStatus::Running);
    CHECK(same.context.moveTarget == first);

    Agent keyed(R"({"wander": {"key": "home", "radius": 3}})");
    keyed.state.blackboard[kHome].set = true;
    keyed.state.blackboard[kHome].vector = Vec3(-4.0f, 0.0f, 0.0f);
    CHECK(keyed.tick() == BehaviorStatus::Running);
    CHECK(keyed.context.lastCenter == Vec3(-4.0f, 0.0f, 0.0f));

    Agent nowhere(R"({"wander": {"radius": 3}})");
    nowhere.context.noPoint = true;
    CHECK(nowhere.tick() == BehaviorStatus::Failure);
    CHECK(nowhere.context.moves == 0);
}

TEST_CASE("stop, setValue and condition finish at once")
{
    Agent agent(R"({"sequence": [{"stop": {}}, {"setValue": {"key": "home", "value": [1, 2, 3]}},
        {"setValue": {"key": "speed", "value": 4.5}}, {"condition": {"key": "speed", "compare": "Greater", "value": 4}},
        {"condition": {"key": "home", "compare": "IsSet"}}, {"setValue": {"key": "home", "value": null}},
        {"condition": {"key": "home", "compare": "IsNotSet"}}]})");
    CHECK(agent.tick() == BehaviorStatus::Success);
    CHECK(agent.context.stops == 1);
    CHECK_FALSE(agent.state.blackboard[kHome].set);
    CHECK(agent.state.blackboard[2].number == 4.5f);
}

TEST_CASE("an action runs until it is finished and reports its node")
{
    Agent agent(R"({"sequence": [{"stop": {}}, {"name": "attack", "action": "bite"}]})");
    CHECK(agent.tick() == BehaviorStatus::Running);
    REQUIRE(agent.context.actions == 1);
    CHECK(agent.context.actionNodes[0] == 2);
    CHECK(agent.tree.nodes[2].name == "attack");
    CHECK(runningNode(agent.tree, agent.state) == 2);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.actions == 1);
    agent.context.finish(1, false);
    CHECK(agent.tick() == BehaviorStatus::Failure);
    CHECK(runningNode(agent.tree, agent.state) == -1);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.context.actions == 2);
}

TEST_CASE("the same seed gives the same statuses and moves")
{
    constexpr std::string_view kRoot = R"({"selector": [
        {"guard": {"key": "flag", "compare": "Equal", "value": true, "abort": "LowerPriority",
                   "child": {"repeat": {"count": 2, "child": {"wait": {"seconds": 0.5, "deviation": 0.4}}}}}},
        {"sequence": [{"wander": {"radius": 5}}, {"wait": {"seconds": 1, "deviation": 0.9}}]}]})";
    const auto run = [&](u64 seed, std::array<BehaviorStatus, 64>& statuses, std::array<f32, 64>& targets) {
        Agent agent(kRoot, seed);
        for (usize i = 0; i < statuses.size(); ++i)
        {
            agent.state.blackboard[kFlag].flag = i % 13 > 8;
            if (agent.context.move == BehaviorStatus::Running && i % 3 == 0)
            {
                agent.context.move = BehaviorStatus::Success;
            }
            statuses.at(i) = agent.tick(0.25f);
            targets.at(i) = agent.context.moveTarget.x;
        }
    };
    std::array<BehaviorStatus, 64> first{};
    std::array<BehaviorStatus, 64> second{};
    std::array<BehaviorStatus, 64> other{};
    std::array<f32, 64> firstTargets{};
    std::array<f32, 64> secondTargets{};
    std::array<f32, 64> otherTargets{};
    run(42, first, firstTargets);
    run(42, second, secondTargets);
    run(43, other, otherTargets);
    CHECK(first == second);
    CHECK(firstTargets == secondTargets);
    CHECK(firstTargets != otherTargets);
}

TEST_CASE("ticking a tree does not allocate")
{
    Agent agent(R"({"parallel": {"successes": 1, "children": [
        {"selector": [
          {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
                     "child": {"timeLimit": {"seconds": 3, "child": {"moveTo": {"key": "target"}}}}}},
          {"cooldown": {"seconds": 1, "child": {"retry": {"count": 1, "child": {"action": "sniff"}}}}},
          {"sequence": [{"wander": {"key": "home", "radius": 4}}, {"wait": {"seconds": 1, "deviation": 0.5}},
                        {"inverter": {"child": {"condition": {"key": "count", "compare": "Less", "value": 0}}}},
                        {"succeeder": {"child": {"failer": {"child": {"stop": {}}}}}},
                        {"setValue": {"key": "count", "value": 3}}]}]},
        {"repeat": {"child": {"setValue": {"key": "flag", "value": true}}}}]}})");
    agent.context.entities[0] = Vec3(5.0f, 0.0f, 0.0f);
    const test::ScopedAllocationCounter counter;
    for (u32 i = 0; i < 200; ++i)
    {
        if (i % 17 == 5)
        {
            agent.setTarget(0);
        }
        if (i % 17 == 12)
        {
            agent.state.blackboard[kTarget].set = false;
        }
        if (agent.context.actions > 0)
        {
            agent.context.finish(agent.context.actions, i % 2 == 0);
        }
        if (agent.context.move == BehaviorStatus::Running && i % 4 == 0)
        {
            agent.context.move = BehaviorStatus::Success;
        }
        (void)agent.tick(0.25f);
    }
    CHECK(counter.count() == 0);
}

TEST_CASE("resetting a tree restores the defaults and keeps the random stream")
{
    Agent agent(R"({"sequence": [{"setValue": {"key": "count", "value": 5}}, {"action": "a"}]})", 3);
    CHECK(agent.tick() == BehaviorStatus::Running);
    CHECK(agent.state.blackboard[kCount].integer == 5);
    CHECK(agent.state.ticks == 1);
    const Random::State random = agent.state.random.state();
    resetBehaviorTree(agent.tree, agent.state);
    CHECK(agent.state.blackboard[kCount].integer == 0);
    CHECK(agent.state.ticks == 0);
    CHECK(agent.state.time == 0.0);
    CHECK(runningNode(agent.tree, agent.state) == -1);
    CHECK(agent.state.random.state() == random);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("ticking a state made for another tree asserts")
{
    Agent agent(R"({"stop": {}})");
    const BehaviorTree other = parse(R"({"sequence": [{"stop": {}}, {"stop": {}}]})");
    const test::ScopedAssertCatcher catcher;
    CHECK_THROWS_AS((void)tickBehaviorTree(other, agent.state, 1.0f, agent.context), test::AssertionFailure);
    CHECK_THROWS_AS((void)agent.tick(-1.0f), test::AssertionFailure);
}
#endif
