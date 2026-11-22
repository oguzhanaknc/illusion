#include "Budget.hpp"

#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/ai/BehaviorTreeState.hpp>
#include <illusion/core/Assert.hpp>

#include <optional>
#include <string_view>
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
