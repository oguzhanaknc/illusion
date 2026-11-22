#pragma once

/// @file
/// The live side of a behaviour tree (docs/adr/0062-navigation-and-ai.md): the state of one agent running a
/// tree, the context through which its leaves act, and the tick. No World and no navigation: AiWorld
/// implements the context per agent and ticks the trees on the Job System.
///
/// @code
/// illusion::BehaviorTreeState state = illusion::makeBehaviorTreeState(tree, seed);
/// state.blackboard[tree.findKey("home")].vector = spawnPoint;
/// state.blackboard[tree.findKey("home")].set = true;
/// const illusion::BehaviorStatus status = illusion::tickBehaviorTree(tree, state, step, context);
/// @endcode

#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/ai/Blackboard.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/Entity.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace illusion
{

/// Status of a node or a tree.
enum class BehaviorStatus : u8
{
    Idle,    ///< Not started since the tree (re)started or the node ended.
    Running, ///< Started and not finished.
    Success, ///< Finished successfully; kept until the node starts again.
    Failure, ///< Finished unsuccessfully; kept until the node starts again.
};

/// The live state of one node.
struct BehaviorNodeState
{
    BehaviorStatus status = BehaviorStatus::Idle; ///< Its status after the last tick.
    u32 child = 0;          ///< Sequence, Selector: the running child; Repeat, Retry: runs so far.
    f32 timer = 0.0f;       ///< Wait, TimeLimit: seconds left.
    u32 handle = 0;         ///< Action, MoveTo, Wander: the context's handle of the started request.
    bool condition = false; ///< Guard: the condition's value when it was last evaluated.
    Vec3 target{0.0f};      ///< MoveTo, Wander: the point of the latest move request.
    f64 readyAt = 0.0;      ///< Cooldown: the BehaviorTreeState::time from which it may run again.
};

/// The state of one agent running a tree.
struct BehaviorTreeState
{
    std::vector<BehaviorNodeState> nodes;    ///< One per node of the tree.
    std::vector<BlackboardValue> blackboard; ///< One per key of the tree.
    Random random;                           ///< Seeded per agent: Wait deviations and Wander points.
    u64 ticks = 0;                           ///< Ticks since the tree (re)started.
    f64 time = 0.0;                          ///< Seconds ticked since the tree (re)started.
    Vec3 origin{0.0f}; ///< The agent's position at the first tick; Wander without a key moves around it.
};

/// A state for @p tree with every node Idle, the keys' defaults and a Random seeded with @p seed. Allocates.
[[nodiscard]] BehaviorTreeState makeBehaviorTreeState(const BehaviorTree& tree, u64 seed);

/// Sets every node of @p state Idle, the blackboard back to @p tree's defaults and the ticks and time to 0;
/// keeps the Random. Running requests are not stopped (the caller does that).
void resetBehaviorTree(const BehaviorTree& tree, BehaviorTreeState& state) noexcept;

/// What the leaves of a tree do to the world, for one agent. AiWorld buffers the requests per agent and
/// applies them after every tree has ticked; tests use a fake.
class BehaviorContext
{
public:
    /// The agent's position in world space.
    [[nodiscard]] virtual Vec3 selfPosition() const = 0;

    /// The position of @p entity, or std::nullopt when it is not alive.
    [[nodiscard]] virtual std::optional<Vec3> position(Entity entity) const = 0;

    /// A reachable navmesh point within @p radius of @p center drawn with @p random, or std::nullopt.
    [[nodiscard]] virtual std::optional<Vec3> randomPoint(const Vec3& center, f32 radius, Random& random) = 0;

    /// Starts moving the agent to @p target, arriving within @p acceptance; returns a handle.
    virtual u32 requestMove(const Vec3& target, f32 acceptance) = 0;

    /// Running while the move of @p handle goes on, then Success (arrived) or Failure (no path, stopped,
    /// replaced by another move).
    [[nodiscard]] virtual BehaviorStatus moveStatus(u32 handle) const = 0;

    /// Stops the agent; a running move fails.
    virtual void stopMoving() = 0;

    /// Starts the action @p name of the Action node @p node; returns a handle.
    virtual u32 startAction(u32 node, std::string_view name) = 0;

    /// Running until a module finishes the action of @p handle, then Success or Failure.
    [[nodiscard]] virtual BehaviorStatus actionStatus(u32 handle) const = 0;

    /// Aborts the running action of @p handle (reported to modules as Aborted).
    virtual void abortAction(u32 handle) = 0;

protected:
    BehaviorContext() = default;
    virtual ~BehaviorContext() = default;
};

/// Ticks @p tree once for @p seconds: guards re-check their conditions (and abort as BehaviorAbort says),
/// running nodes continue, finished composites move on, and the root's status is returned; a root that
/// finished starts again at the next tick. Aborted MoveTo, Wander and Action nodes stop their requests
/// through @p context. Does not allocate.
BehaviorStatus tickBehaviorTree(const BehaviorTree& tree, BehaviorTreeState& state, f32 seconds,
                                BehaviorContext& context);

/// Index of the deepest Running node of @p state (the last in pre-order), or -1.
[[nodiscard]] i32 runningNode(const BehaviorTree& tree, const BehaviorTreeState& state) noexcept;

} // namespace illusion
