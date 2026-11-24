#pragma once

/// @file
/// The live side of an animation graph (docs/adr/0060-animation-graphs.md): a graph bound to one skeleton,
/// the state of one Animator playing it, and the two steps of an update. No World, no assets loading:
/// AnimationWorld resolves the clips and calls these per Animator on the Job System.
///
/// @code
/// auto binding = illusion::bindGraph(graph, skeleton, findClip); // once per graph and skeleton
/// illusion::AnimationGraphState state = illusion::makeGraphState(*binding);
/// state.setFloat(graph.findParameter("speed"), 3.0f);
/// std::array<illusion::GraphEvent, 8> events{};
/// const illusion::u32 fired = illusion::stepGraph(*binding, state, deltaSeconds, events);
/// illusion::RootMotionDelta root;
/// illusion::evaluateGraph(*binding, state, scratch, localPose, &root);
/// @endcode

#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/Pose.hpp>
#include <illusion/animation/Retarget.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace illusion
{

class LinearAllocator;

/// An AnimationGraph whose clips are resolved and bound to one Skeleton. Immutable after bindGraph(); shared
/// by every Animator with that graph and skeleton.
struct GraphBinding
{
    const AnimationGraph* graph = nullptr; ///< The graph (outlives the binding).
    const Skeleton* skeleton = nullptr;    ///< The skeleton (outlives the binding).
    /// One clip per clip reference of the graph, state by state and point by point in file order.
    std::vector<const AnimationClip*> clips;
    /// The binding of each entry of clips: bindClip(), or the binding of bindRetargetedClip().
    std::vector<ClipBinding> clipBindings;
    /// The retargeting of each entry of clips; inactive for a clip bound by name.
    std::vector<ClipRetarget> clipRetargets;
    /// The pose evaluation starts from: the reference pose of the first retargeted clip, or empty for the
    /// skeleton's rest pose.
    std::vector<JointPose> basePose;
    /// Per layer: index into clips of AnimationLayer::reference (after every state's clips), or -1.
    std::vector<i32> referenceClips;
    std::vector<u32> firstClip;  ///< Per state, all layers flattened in order: index of its first clip.
    std::vector<u32> firstState; ///< Per layer: index of its first state in the flattened state order.
    /// Per layer: one weight per joint, or empty for a layer without a mask.
    std::vector<std::vector<f32>> masks;
    i32 rootJoint = -1; ///< Joint of AnimationGraph::rootMotion, or -1.
};

/// Resolves every clip of @p graph (states, then layer references) with @p findClip (null when missing) and
/// binds it to @p skeleton by name.
/// Mask and root motion joints are found with findRigJoint(). Fails naming the first clip that is missing, a
/// mask or root motion joint the skeleton lacks. Allocates.
[[nodiscard]] Expected<GraphBinding, std::string>
bindGraph(const AnimationGraph& graph, const Skeleton& skeleton,
          const std::function<const AnimationClip*(const AssetRef&)>& findClip);

/// As bindGraph(), but @p findClip also returns each clip's source skeleton: a clip with one is bound with
/// bindRetargetedClip(), one without (or one that cannot be retargeted, logged) by name.
[[nodiscard]] Expected<GraphBinding, std::string>
bindGraph(const AnimationGraph& graph, const Skeleton& skeleton,
          const std::function<ClipSource(const AssetRef&)>& findClip);

/// The machine state of one layer.
struct GraphLayerState
{
    u32 state = 0;               ///< Current (target) state.
    f32 time = 0.0f;             ///< Its normalized time (may exceed 1 for a held non-looping state).
    i32 previous = -1;           ///< State faded out by a running transition, or -1.
    f32 previousTime = 0.0f;     ///< Its normalized time.
    f32 fade = 0.0f;             ///< Seconds of the running transition so far.
    f32 fadeDuration = 0.0f;     ///< Its length in seconds (0: none running).
    f32 lastTime = 0.0f;         ///< time before the last stepGraph() (root motion and events).
    f32 lastPreviousTime = 0.0f; ///< previousTime before the last stepGraph().
};

/// The live state of one Animator playing a graph: parameter values, set triggers and the layers' machines.
struct AnimationGraphState
{
    std::vector<f32> parameters; ///< Value per parameter (Int and Bool as numbers).
    /// Per parameter: 1 for a set trigger, 0 for an unset one, kNotTrigger for a Float, Int or Bool.
    std::vector<u8> triggers;
    std::vector<GraphLayerState> layers; ///< Per layer.

    /// Value of AnimationGraphState::triggers for a parameter that is not a Trigger.
    static constexpr u8 kNotTrigger = 2;

    /// Sets parameter @p index (ignored when out of range, not a Float/Int/Bool, or not finite).
    void setValue(i32 index, f32 value) noexcept;
    /// Sets the trigger @p index (ignored when out of range or not a Trigger).
    void setTrigger(i32 index) noexcept;
};

/// The entry state of every layer and the default parameters of @p binding's graph. Allocates.
[[nodiscard]] AnimationGraphState makeGraphState(const GraphBinding& binding);

/// Seconds of one loop of state @p stateIndex of layer @p layer with the parameters of @p state: its clip's
/// duration, or the weighted duration of its blend space. 0 for indices out of range.
[[nodiscard]] f32 graphStateDuration(const GraphBinding& binding, const AnimationGraphState& state, u32 layer,
                                     u32 stateIndex) noexcept;

/// Bytes of LinearAllocator scratch that evaluateGraph() needs for @p binding (alignment included).
[[nodiscard]] usize graphScratchBytes(const GraphBinding& binding) noexcept;

/// One state event that an update crossed.
struct GraphEvent
{
    u16 layer = 0; ///< Layer index.
    u16 state = 0; ///< State index in the layer.
    u32 event = 0; ///< Index into AnimationState::events.
};

/// Advances @p state by @p seconds: finishes or starts transitions (the first whose conditions hold, in
/// priority order; a fired transition consumes its triggers), advances normalized times by speed (blend
/// spaces by the weighted duration of their clips), then clears unconsumed triggers. Writes the events
/// crossed by states with weight above 0, in layer order, into @p events and returns how many there were
/// (more than `events.size()` when it was too small). Does not allocate.
u32 stepGraph(const GraphBinding& binding, AnimationGraphState& state, f32 seconds,
              std::span<GraphEvent> events) noexcept;

/// Root motion taken out of a pose by evaluateGraph(), in the skeleton's model space.
struct RootMotionDelta
{
    Vec3 translation{0.0f}; ///< Horizontal (x, z) motion of the root motion joint since the last step.
    f32 yaw = 0.0f;         ///< Its rotation about +y since the last step, in radians.
};

/// Writes the local pose of @p state into @p out (one entry per joint): samples the weighted clips (at most
/// kMaxActiveClips), blends blend spaces and transitions, applies the layers with their masks and modes (an
/// Additive layer against its reference pose, in local or model space; ADR 0065). With a root motion joint
/// and @p root, computes the joint's motion from each clip's time before and after the last stepGraph()
/// (across a loop wrap), weighted, into @p root, and removes it from the pose. Scratch poses come from
/// @p scratch, which the caller rewinds. Does not allocate on the heap.
void evaluateGraph(const GraphBinding& binding, const AnimationGraphState& state, LinearAllocator& scratch,
                   std::span<JointPose> out, RootMotionDelta* root) noexcept;

} // namespace illusion
