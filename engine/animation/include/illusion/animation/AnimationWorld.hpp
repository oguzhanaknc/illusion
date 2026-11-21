#pragma once

/// @file
/// Evaluates the Animators of a World on the Job System and keeps their poses outside the ECS
/// (docs/adr/0059-skeletal-animation.md).
///
/// @code
/// illusion::AnimationWorld animation(world, streamer);
/// while (running)
/// {
///     streamer.update();
///     animation.update(deltaSeconds, &jobs, true); // after the modules, before physics
///     renderer.render(world, animation, ...);      // reads skinMatrices()
/// }
/// @endcode

#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationService.hpp>
#include <illusion/animation/Pose.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>

#include <memory>
#include <optional>
#include <span>

namespace illusion
{

class AssetStreamer;
class JobSystem;
class World;

/// Counters of an AnimationWorld.
struct AnimationStatistics
{
    u32 poses = 0;    ///< Animators with a pose slot.
    u32 posed = 0;    ///< Poses written by the last update().
    u32 pending = 0;  ///< Animators waiting for their model after the last update().
    u32 failures = 0; ///< Animators whose skeleton or clip could not be found, since construction.
    u64 joints = 0;   ///< Joints evaluated by the last update().
};

/// Keeps one pose per Animator of a World. update() loads the models of new or changed Animators through an
/// AssetStreamer (each skeleton and clip once, shared; a clip bound to a skeleton once per pair), adds
/// AnimationPose, advances Animator::time when asked, samples and composes every pose in parallel, writes the
/// skin matrices of the frame into one contiguous buffer, keeps the previous frame's for motion vectors, and
/// computes each pose's world bounds from its mesh's joint bounds and the Entity's Transform. Entities with
/// the Inactive tag keep their last pose. A skeleton whose joint count differs from the skinned mesh's is
/// reported once and gets no pose (the mesh is drawn in its bind pose). Deterministic; allocates only when
/// poses are added, assets load or capacity grows. Live reload of models re-binds their Animators. Use on the
/// World's thread.
///
/// An Animator with Animator::graph plays that `.animgraph` file (ADR 0060) instead of its clip: the graph is
/// loaded once and bound once per skeleton, and each Animator keeps its own AnimationGraphState, which starts
/// in the entry states with Animator::time as their time in seconds and the default parameters. While
/// advancing and Animator::playing, the graph steps by Animator::speed times the update's seconds (a negative
/// speed holds it); without advancing, it shows the entry states at Animator::time. The state events crossed
/// by an update are published for that update (events()), and a graph with root motion writes the joint's
/// motion, turned into world space by the Entity's Transform, into AnimationPose::rootTranslation and
/// AnimationPose::rootYaw. A changed graph file (AssetReloadedEvent) restarts its Animators. An Animator
/// without a graph cross-fades from its previous clip after crossFade().
///
/// After every pose of an update is evaluated, each active Entity with a BoneAttachment whose parent
/// (Hierarchy) has a posed Animator follows its joint (a socket, ADR 0061): its LocalTransform becomes the
/// joint's model-space transform times the attachment's offset, and its Transform follows from the parent's
/// current Transform. The joint name is looked up once per skeleton, again after a model reload or a change
/// of BoneAttachment::joint. A parent without an Animator or a missing joint is logged once per Entity, which
/// then stays where it is; a parent whose model loads leaves it alone silently.
///
/// Before the model-space matrices, each pose goes through the stages of ADR 0061 in its Job: the pelvis
/// offset and foot targets of a FootPlacement (from its FootPlacementPose, which this class adds and whose
/// animated placements it writes), the LookAt of the Entity (following its target at LookAt::speed while
/// advancing, at once otherwise) and the TwoBoneIk limbs on the Entity and its children. Targets are read
/// when the update starts and turned into model space through the Entity's Transform.
class AnimationWorld : NonCopyable
{
public:
    /// @p world and @p streamer must outlive this object.
    AnimationWorld(World& world, AssetStreamer& streamer);
    ~AnimationWorld();

    /// Advances the frame counter, then poses every Animator: with @p advance, adds speed times
    /// @p deltaSeconds to Animator::time first (wrapped or clamped with clipTime(); a clip that stops sets
    /// playing to false). Without @p advance (the editor outside Play mode) the saved time is shown as it is.
    /// Runs as parallel Jobs on @p jobs when given. Adds AnimationPose, so call it outside queries.
    void update(f32 deltaSeconds, JobSystem* jobs, bool advance);

    /// Calls update(0, nullptr, false) and waits for the streamer until no Animator waits for its model.
    void waitIdle();

    /// Number of the last update(); starts at 0.
    [[nodiscard]] u64 frame() const noexcept;

    /// The skin matrices of @p slot written by the last update() (AnimationPose::jointCount entries).
    [[nodiscard]] std::span<const SkinMatrix> skinMatrices(u32 slot) const noexcept;

    /// The skin matrices of @p slot from the update before the last; equal to skinMatrices() for a pose's
    /// first frame.
    [[nodiscard]] std::span<const SkinMatrix> previousSkinMatrices(u32 slot) const noexcept;

    /// Model-space matrices of @p slot's joints from the last update() (for sockets and debug drawing).
    [[nodiscard]] std::span<const Mat4> jointMatrices(u32 slot) const noexcept;

    /// The skeleton of @p slot, or null while it loads.
    [[nodiscard]] const Skeleton* skeleton(u32 slot) const noexcept;

    /// Counters.
    [[nodiscard]] const AnimationStatistics& statistics() const noexcept;

    /// The World it animates.
    [[nodiscard]] World& world() const noexcept;

    /// The pose slot of @p entity's Animator (AnimationPose::slot), or std::nullopt before its first
    /// update().
    [[nodiscard]] std::optional<u32> findSlot(Entity entity) const noexcept;

    /// The graph that drives @p slot, or null without a graph or while it loads (or failed to load or bind).
    [[nodiscard]] const AnimationGraph* graph(u32 slot) const noexcept;

    /// The graph state of @p slot (parameters and machines after the last update()), or null when graph() is.
    [[nodiscard]] const AnimationGraphState* graphState(u32 slot) const noexcept;

    /// Sets parameter @p index of @p slot's graph for its next step (AnimationGraphState::setValue()); false
    /// when graph() is null.
    bool setParameter(u32 slot, i32 index, f32 value) noexcept;

    /// Sets the trigger @p index of @p slot's graph for its next step; false when graph() is null.
    bool setTrigger(u32 slot, i32 index) noexcept;

    /// Plays @p clip on @p entity's Animator from time 0 (and sets Animator::playing), cross-fading from its
    /// current clip over @p seconds while both advance. False, changing nothing, if the Entity has no
    /// Animator or its Animator has a graph.
    bool crossFade(Entity entity, const AssetRef& clip, f32 seconds);

    /// The state events of the last update(), in update order; their names stay valid until the next one.
    [[nodiscard]] std::span<const AnimationEvent> events() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
