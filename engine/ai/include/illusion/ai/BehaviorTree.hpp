#pragma once

/// @file
/// Behaviour trees (docs/adr/0062-navigation-and-ai.md): the content of a `.behavior` file, blackboard keys
/// and nodes flattened in pre-order. Plain data and a parser; the live state and the tick are in
/// BehaviorTreeState.hpp.
///
/// @code
/// {
///   "format": "illusion.behavior", "version": 1,
///   "blackboard": [{"name": "target", "type": "Entity"}, {"name": "home", "type": "Vector3"}],
///   "root": {"selector": [
///     {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
///       "child": {"sequence": [{"moveTo": {"key": "target", "acceptance": 1.5}}, {"action": "bite"}]}}},
///     {"sequence": [{"wander": {"key": "home", "radius": 8}}, {"wait": {"seconds": 2, "deviation": 1}}]}
///   ]}
/// }
/// @endcode

#include <illusion/ai/Blackboard.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// File extension of behaviour trees.
inline constexpr std::string_view kBehaviorTreeExtension = ".behavior";

/// Version of the `.behavior` format that parseBehaviorTree() writes and reads.
inline constexpr u32 kBehaviorTreeVersion = 1;

/// Most nodes of a behaviour tree.
inline constexpr u32 kMaxBehaviorNodes = 1024;

/// Kind of BehaviorNode. Composites have children, decorators one child, leaves none.
enum class BehaviorNodeType : u8
{
    Sequence,  ///< Runs children in order until one fails.
    Selector,  ///< Runs children in order until one succeeds.
    Parallel,  ///< Runs every child each tick; succeeds once BehaviorNode::count succeed (0: all).
    Inverter,  ///< Swaps its child's success and failure.
    Succeeder, ///< Succeeds when its child ends.
    Failer,    ///< Fails when its child ends.
    Repeat,    ///< Runs its child BehaviorNode::count times (0: forever) unless it fails.
    Retry,     ///< Runs its child again after a failure, up to BehaviorNode::count times (0: forever).
    Cooldown,  ///< Fails for BehaviorNode::seconds after its child ended.
    TimeLimit, ///< Fails, aborting its child, after BehaviorNode::seconds.
    Guard,     ///< Runs its child while a blackboard condition holds; see BehaviorAbort.
    Wait,      ///< Running for BehaviorNode::seconds, plus or minus BehaviorNode::deviation.
    MoveTo,    ///< Moves the NavAgent to a Vector3 or Entity key, within BehaviorNode::radius.
    Wander,    ///< Moves to a random navmesh point within BehaviorNode::radius of a Vector3 key or the start.
    Stop,      ///< Stops the NavAgent; succeeds at once.
    SetValue,  ///< Writes BehaviorNode::value to a key; succeeds at once.
    Condition, ///< Succeeds when a blackboard condition holds, otherwise fails.
    Action,    ///< Game code: Running until a module finishes it (AiService::finishAction()).
};

/// Name of @p type as a `.behavior` file writes it, e.g. "moveTo".
[[nodiscard]] constexpr std::string_view behaviorNodeTypeName(BehaviorNodeType type) noexcept
{
    switch (type)
    {
    case BehaviorNodeType::Sequence:
        return "sequence";
    case BehaviorNodeType::Selector:
        return "selector";
    case BehaviorNodeType::Parallel:
        return "parallel";
    case BehaviorNodeType::Inverter:
        return "inverter";
    case BehaviorNodeType::Succeeder:
        return "succeeder";
    case BehaviorNodeType::Failer:
        return "failer";
    case BehaviorNodeType::Repeat:
        return "repeat";
    case BehaviorNodeType::Retry:
        return "retry";
    case BehaviorNodeType::Cooldown:
        return "cooldown";
    case BehaviorNodeType::TimeLimit:
        return "timeLimit";
    case BehaviorNodeType::Guard:
        return "guard";
    case BehaviorNodeType::Wait:
        return "wait";
    case BehaviorNodeType::MoveTo:
        return "moveTo";
    case BehaviorNodeType::Wander:
        return "wander";
    case BehaviorNodeType::Stop:
        return "stop";
    case BehaviorNodeType::SetValue:
        return "setValue";
    case BehaviorNodeType::Condition:
        return "condition";
    case BehaviorNodeType::Action:
        return "action";
    }
    return "sequence";
}

/// Acceptance radius in m of the moves of a Wander node.
inline constexpr f32 kWanderAcceptance = 0.5f;

/// What a Guard aborts when its condition changes.
enum class BehaviorAbort : u8
{
    None,          ///< Checked only when the guard starts.
    Self,          ///< Aborts its running child when the condition turns false.
    LowerPriority, ///< Also aborts a running later sibling of its selector when the condition turns true.
};

/// One node. Only the fields its type uses are meaningful.
struct BehaviorNode
{
    BehaviorNodeType type = BehaviorNodeType::Sequence; ///< Its kind.
    std::string name;   ///< Label (status and debugging); defaults to behaviorNodeTypeName().
    u32 firstChild = 0; ///< Index of its first child in BehaviorTree::children.
    u32 childCount = 0; ///< Children: composites one or more, decorators one, leaves none.
    i32 key = -1;       ///< Guard, Condition, MoveTo, Wander, SetValue: blackboard key index, or -1.
    BlackboardCompare compare = BlackboardCompare::IsSet; ///< Guard, Condition: comparison.
    BlackboardValue value;                                ///< Guard, Condition: operand; SetValue: value.
    BehaviorAbort abort = BehaviorAbort::None;            ///< Guard: abort mode.
    f32 seconds = 0.0f;                                   ///< Wait, Cooldown, TimeLimit: duration.
    f32 deviation = 0.0f; ///< Wait: random deviation of seconds, at most seconds.
    f32 radius = 1.0f;    ///< MoveTo: acceptance radius; Wander: radius, in m.
    u32 count = 0;        ///< Parallel, Repeat, Retry: see the types.
    std::string action;   ///< Action: the name modules see.
};

/// The content of a `.behavior` file.
struct BehaviorTree
{
    std::vector<BlackboardKey> blackboard; ///< Keys, at most kMaxBlackboardKeys.
    std::vector<BehaviorNode> nodes;       ///< Nodes in pre-order; the root is node 0.
    std::vector<u32> children;             ///< Child node indices, each node's children contiguous.

    /// Index of the key @p name, or -1.
    [[nodiscard]] i32 findKey(std::string_view name) const noexcept;
};

/// Parses and validates a `.behavior` file (unknown keys are errors, key names unique and resolved, each
/// node's fields fit its type, values finite, at most kMaxBehaviorNodes nodes). An error message names the
/// JSON path of the problem, e.g. "root.selector[0].guard.key: unknown key 'targt'".
[[nodiscard]] Expected<BehaviorTree, std::string> parseBehaviorTree(std::string_view json);

/// Writes @p tree as `.behavior` JSON (format tag and kBehaviorTreeVersion); parseBehaviorTree() reads it
/// back unchanged.
[[nodiscard]] std::string writeBehaviorTree(const BehaviorTree& tree);

} // namespace illusion
