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

#include <illusion/animation/Pose.hpp>
#include <illusion/core/Base.hpp>

#include <memory>
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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
