#include <illusion/ai/BehaviorTreeState.hpp>
#include <illusion/core/Assert.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{
namespace
{

class Ticker
{
public:
    Ticker(const BehaviorTree& tree, BehaviorTreeState& state, f32 seconds, BehaviorContext& context) noexcept
        : m_tree(tree)
        , m_state(state)
        , m_context(context)
        , m_seconds(seconds)
    {
    }

    BehaviorStatus tick(u32 index)
    {
        BehaviorNodeState& state = m_state.nodes[index];
        const bool starting = state.status != BehaviorStatus::Running;
        state.status = run(index, state, starting);
        return state.status;
    }

private:
    [[nodiscard]] u32 child(const BehaviorNode& node, u32 i) const noexcept
    {
        return m_tree.children[node.firstChild + i];
    }

    // Sets the Running nodes of the subtree at @p index Idle, stopping their moves and actions.
    void abort(u32 index)
    {
        u32 last = index;
        while (m_tree.nodes[last].childCount > 0)
        {
            const BehaviorNode& node = m_tree.nodes[last];
            last = child(node, node.childCount - 1);
        }
        bool stopped = false;
        for (u32 i = index; i <= last; ++i)
        {
            BehaviorNodeState& state = m_state.nodes[i];
            if (state.status != BehaviorStatus::Running)
            {
                continue;
            }
            const BehaviorNodeType type = m_tree.nodes[i].type;
            if ((type == BehaviorNodeType::MoveTo || type == BehaviorNodeType::Wander) && !stopped)
            {
                m_context.stopMoving();
                stopped = true;
            }
            else if (type == BehaviorNodeType::Action)
            {
                m_context.abortAction(state.handle);
            }
            state.status = BehaviorStatus::Idle;
        }
    }

    [[nodiscard]] bool condition(const BehaviorNode& node) const
    {
        const BlackboardValue& value = m_state.blackboard[static_cast<usize>(node.key)];
        const bool alive = value.type != BlackboardType::Entity ||
                           (value.set && m_context.position(value.entity).has_value());
        return compareBlackboard(value, node.compare, node.value, alive);
    }

    // The point a MoveTo node goes to: its Vector3 key, or the position of its Entity key.
    [[nodiscard]] std::optional<Vec3> destination(const BehaviorNode& node) const
    {
        const BlackboardValue& value = m_state.blackboard[static_cast<usize>(node.key)];
        if (!value.set)
        {
            return std::nullopt;
        }
        if (value.type == BlackboardType::Entity)
        {
            return m_context.position(value.entity);
        }
        return value.vector;
    }

    static BehaviorStatus finished(BehaviorStatus status) noexcept
    {
        return status == BehaviorStatus::Success   ? BehaviorStatus::Success
               : status == BehaviorStatus::Running ? BehaviorStatus::Running
                                                   : BehaviorStatus::Failure;
    }

    BehaviorStatus run(u32 index, BehaviorNodeState& state, bool starting)
    {
        const BehaviorNode& node = m_tree.nodes[index];
        switch (node.type)
        {
        case BehaviorNodeType::Sequence:
            if (starting)
            {
                state.child = 0;
            }
            for (; state.child < node.childCount; ++state.child)
            {
                const BehaviorStatus status = tick(child(node, state.child));
                if (status != BehaviorStatus::Success)
                {
                    return status;
                }
            }
            return BehaviorStatus::Success;
        case BehaviorNodeType::Selector:
            if (starting)
            {
                state.child = 0;
            }
            else
            {
                preempt(node, state);
            }
            for (; state.child < node.childCount; ++state.child)
            {
                const BehaviorStatus status = tick(child(node, state.child));
                if (status != BehaviorStatus::Failure)
                {
                    return status;
                }
            }
            return BehaviorStatus::Failure;
        case BehaviorNodeType::Parallel:
            return parallel(node, starting);
        case BehaviorNodeType::Inverter:
        {
            const BehaviorStatus status = tick(child(node, 0));
            return status == BehaviorStatus::Success   ? BehaviorStatus::Failure
                   : status == BehaviorStatus::Failure ? BehaviorStatus::Success
                                                       : status;
        }
        case BehaviorNodeType::Succeeder:
        {
            const BehaviorStatus status = tick(child(node, 0));
            return status == BehaviorStatus::Running ? status : BehaviorStatus::Success;
        }
        case BehaviorNodeType::Failer:
        {
            const BehaviorStatus status = tick(child(node, 0));
            return status == BehaviorStatus::Running ? status : BehaviorStatus::Failure;
        }
        case BehaviorNodeType::Repeat:
        {
            if (starting)
            {
                state.child = 0;
            }
            // One run of the child per tick, so a child that succeeds at once cannot loop within a tick.
            const BehaviorStatus status = tick(child(node, 0));
            if (status != BehaviorStatus::Success)
            {
                return status;
            }
            ++state.child;
            return node.count != 0 && state.child >= node.count ? BehaviorStatus::Success
                                                                : BehaviorStatus::Running;
        }
        case BehaviorNodeType::Retry:
        {
            if (starting)
            {
                state.child = 0;
            }
            const BehaviorStatus status = tick(child(node, 0));
            if (status != BehaviorStatus::Failure)
            {
                return status;
            }
            ++state.child;
            return node.count != 0 && state.child > node.count ? BehaviorStatus::Failure
                                                               : BehaviorStatus::Running;
        }
        case BehaviorNodeType::Cooldown:
        {
            if (starting && m_state.time < state.readyAt)
            {
                return BehaviorStatus::Failure;
            }
            const BehaviorStatus status = tick(child(node, 0));
            if (status != BehaviorStatus::Running)
            {
                state.readyAt = m_state.time + static_cast<f64>(node.seconds);
            }
            return status;
        }
        case BehaviorNodeType::TimeLimit:
        {
            if (starting)
            {
                state.timer = node.seconds;
            }
            const BehaviorStatus status = tick(child(node, 0));
            if (status != BehaviorStatus::Running)
            {
                return status;
            }
            state.timer -= m_seconds;
            if (state.timer > 0.0f)
            {
                return status;
            }
            abort(child(node, 0));
            return BehaviorStatus::Failure;
        }
        case BehaviorNodeType::Guard:
        {
            const bool holds = condition(node);
            state.condition = holds;
            if (!holds && (starting || node.abort != BehaviorAbort::None))
            {
                if (!starting)
                {
                    abort(child(node, 0));
                }
                return BehaviorStatus::Failure;
            }
            return tick(child(node, 0));
        }
        case BehaviorNodeType::Wait:
            if (starting)
            {
                state.timer = node.seconds;
                if (node.deviation > 0.0f)
                {
                    state.timer = std::max(
                        0.0f, state.timer + node.deviation * (2.0f * m_state.random.uniformF32() - 1.0f));
                }
            }
            state.timer -= m_seconds;
            return state.timer > 0.0f ? BehaviorStatus::Running : BehaviorStatus::Success;
        case BehaviorNodeType::MoveTo:
        {
            const std::optional<Vec3> target = destination(node);
            if (!target)
            {
                if (!starting)
                {
                    m_context.stopMoving();
                }
                return BehaviorStatus::Failure;
            }
            if (starting)
            {
                const Vec3 offset = *target - m_context.selfPosition();
                if (glm::dot(offset, offset) <= node.radius * node.radius)
                {
                    return BehaviorStatus::Success;
                }
            }
            const Vec3 moved = *target - state.target;
            if (starting || glm::dot(moved, moved) > node.radius * node.radius)
            {
                state.handle = m_context.requestMove(*target, node.radius);
                state.target = *target;
                return BehaviorStatus::Running;
            }
            return finished(m_context.moveStatus(state.handle));
        }
        case BehaviorNodeType::Wander:
        {
            if (!starting)
            {
                return finished(m_context.moveStatus(state.handle));
            }
            const BlackboardValue* center =
                node.key >= 0 ? &m_state.blackboard[static_cast<usize>(node.key)] : nullptr;
            const std::optional<Vec3> point =
                m_context.randomPoint(center != nullptr && center->set ? center->vector : m_state.origin,
                                      node.radius, m_state.random);
            if (!point)
            {
                return BehaviorStatus::Failure;
            }
            state.handle = m_context.requestMove(*point, kWanderAcceptance);
            state.target = *point;
            return BehaviorStatus::Running;
        }
        case BehaviorNodeType::Stop:
            m_context.stopMoving();
            return BehaviorStatus::Success;
        case BehaviorNodeType::SetValue:
            m_state.blackboard[static_cast<usize>(node.key)] = node.value;
            return BehaviorStatus::Success;
        case BehaviorNodeType::Condition:
            return condition(node) ? BehaviorStatus::Success : BehaviorStatus::Failure;
        case BehaviorNodeType::Action:
            if (starting)
            {
                state.handle = m_context.startAction(index, node.action);
                return BehaviorStatus::Running;
            }
            return finished(m_context.actionStatus(state.handle));
        }
        return BehaviorStatus::Failure;
    }

    // A LowerPriority guard before the running child of a selector whose condition turned true takes over:
    // the running child is aborted and the selector continues from the guard.
    void preempt(const BehaviorNode& node, BehaviorNodeState& state)
    {
        for (u32 i = 0; i < state.child && i < node.childCount; ++i)
        {
            const u32 guard = child(node, i);
            const BehaviorNode& candidate = m_tree.nodes[guard];
            if (candidate.type != BehaviorNodeType::Guard || candidate.abort != BehaviorAbort::LowerPriority)
            {
                continue;
            }
            BehaviorNodeState& guardState = m_state.nodes[guard];
            const bool holds = condition(candidate);
            const bool turned = holds && !guardState.condition;
            guardState.condition = holds;
            if (turned)
            {
                abort(child(node, state.child));
                state.child = i;
                return;
            }
        }
    }

    BehaviorStatus parallel(const BehaviorNode& node, bool starting)
    {
        if (starting)
        {
            for (u32 i = 0; i < node.childCount; ++i)
            {
                m_state.nodes[child(node, i)].status = BehaviorStatus::Idle;
            }
        }
        u32 successes = 0;
        u32 failures = 0;
        for (u32 i = 0; i < node.childCount; ++i)
        {
            const u32 index = child(node, i);
            BehaviorStatus status = m_state.nodes[index].status;
            if (status == BehaviorStatus::Idle || status == BehaviorStatus::Running)
            {
                status = tick(index);
            }
            successes += status == BehaviorStatus::Success ? 1u : 0u;
            failures += status == BehaviorStatus::Failure ? 1u : 0u;
        }
        const u32 needed = node.count == 0 ? node.childCount : std::min(node.count, node.childCount);
        BehaviorStatus result = BehaviorStatus::Running;
        if (successes >= needed)
        {
            result = BehaviorStatus::Success;
        }
        else if (failures > node.childCount - needed)
        {
            result = BehaviorStatus::Failure;
        }
        if (result != BehaviorStatus::Running)
        {
            for (u32 i = 0; i < node.childCount; ++i)
            {
                abort(child(node, i));
            }
        }
        return result;
    }

    const BehaviorTree& m_tree;
    BehaviorTreeState& m_state;
    BehaviorContext& m_context;
    f32 m_seconds;
};

} // namespace

BehaviorTreeState makeBehaviorTreeState(const BehaviorTree& tree, u64 seed)
{
    BehaviorTreeState state;
    state.nodes.resize(tree.nodes.size());
    state.blackboard.reserve(tree.blackboard.size());
    for (const BlackboardKey& key : tree.blackboard)
    {
        state.blackboard.push_back(key.defaultValue);
    }
    state.random.seed(seed);
    return state;
}

void resetBehaviorTree(const BehaviorTree& tree, BehaviorTreeState& state) noexcept
{
    std::fill(state.nodes.begin(), state.nodes.end(), BehaviorNodeState{});
    const usize keys = std::min(tree.blackboard.size(), state.blackboard.size());
    for (usize i = 0; i < keys; ++i)
    {
        state.blackboard[i] = tree.blackboard[i].defaultValue;
    }
    state.ticks = 0;
    state.time = 0.0;
    state.origin = Vec3(0.0f);
}

BehaviorStatus tickBehaviorTree(const BehaviorTree& tree, BehaviorTreeState& state, f32 seconds,
                                BehaviorContext& context)
{
    ILL_ASSERT(state.nodes.size() == tree.nodes.size() && state.blackboard.size() == tree.blackboard.size(),
               "the state was not made for this tree (makeBehaviorTreeState())");
    ILL_ASSERT(std::isfinite(seconds) && seconds >= 0.0f, "a tick needs a finite, non-negative step");
    if (tree.nodes.empty())
    {
        return BehaviorStatus::Idle;
    }
    if (state.ticks == 0)
    {
        state.origin = context.selfPosition();
    }
    state.time += static_cast<f64>(seconds);
    Ticker ticker(tree, state, seconds, context);
    const BehaviorStatus status = ticker.tick(0);
    ++state.ticks;
    return status;
}

i32 runningNode(const BehaviorTree& tree, const BehaviorTreeState& state) noexcept
{
    const usize count = std::min(tree.nodes.size(), state.nodes.size());
    for (usize i = count; i > 0; --i)
    {
        if (state.nodes[i - 1].status == BehaviorStatus::Running)
        {
            return static_cast<i32>(i - 1);
        }
    }
    return -1;
}

} // namespace illusion
