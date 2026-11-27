#pragma once

/// @file
/// The DebugDraw visualisers of the Scene views (GPU-free): light ranges and directions, camera frusta,
/// the frusta of virtual cameras (the live and blending ones always), collider shapes, character capsules,
/// joint anchors, axes and limits, audio distances and cones, and the skeletons of Animators with their IK
/// targets, spring chains and ragdoll parts, recorded as world-space lines.
///
/// ```cpp
/// DebugDraw draw(65536);
/// VisualiserOptions options;
/// options.all = true;
/// draw.clear();
/// const VisualiserStats stats = recordVisualisers(world, draw, options);
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <span>

namespace illusion
{
class AnimationWorld;
class DebugDraw;
class World;
struct Aabb;
struct Ragdoll;
struct RagdollPart;
struct Skeleton;
} // namespace illusion

namespace illusion::editor::viewport
{

/// Linear RGBA colours of the visualisers.
struct VisualiserColors
{
    Vec4 light{1.0f, 0.8f, 0.25f, 1.0f};           ///< Light ranges and directions.
    Vec4 camera{0.8f, 0.8f, 0.8f, 1.0f};           ///< Camera frusta.
    Vec4 virtualCamera{0.45f, 0.75f, 0.85f, 1.0f}; ///< Frusta of virtual cameras.
    Vec4 liveCamera{1.0f, 0.85f, 0.2f, 1.0f}; ///< The virtual camera a brain shows (or the previewed one).
    Vec4 blendingCamera{1.0f, 0.5f, 0.15f, 1.0f};  ///< The virtual camera a brain blends from.
    Vec4 collider{0.25f, 0.9f, 0.3f, 1.0f};        ///< Solid colliders.
    Vec4 sensor{0.3f, 0.55f, 1.0f, 1.0f};          ///< Colliders of sensor bodies (triggers).
    Vec4 character{0.2f, 0.85f, 0.85f, 1.0f};      ///< Character capsules.
    Vec4 joint{1.0f, 0.35f, 0.75f, 1.0f};          ///< Joint anchors and axes.
    Vec4 jointLimit{1.0f, 0.55f, 0.15f, 1.0f};     ///< Joint limits.
    Vec4 audioNear{0.45f, 0.6f, 1.0f, 1.0f};       ///< AudioSource minimum distance and inner cone.
    Vec4 audioFar{0.25f, 0.3f, 0.75f, 1.0f};       ///< AudioSource maximum distance and outer cone.
    Vec4 bone{0.35f, 0.65f, 1.0f, 1.0f};           ///< Bones of skeletons.
    Vec4 skeletonJoint{0.95f, 0.95f, 0.95f, 1.0f}; ///< Joints of skeletons.
    Vec4 ikTarget{1.0f, 0.7f, 0.2f, 1.0f};         ///< LookAt and TwoBoneIk targets and poles.
    Vec4 spring{0.3f, 0.85f, 0.45f, 1.0f};         ///< SpringBone chains and colliders.
    Vec4 ragdoll{0.95f, 0.4f, 0.4f, 1.0f};         ///< Ragdoll parts.
    f32 unmappedAlpha = 0.3f; ///< Alpha factor of joints without a bone in their rig map, and of their bones.
};

/// Finds the parts of a Ragdoll for the skeleton visualiser (the viewports read and cache `.ragdoll` files).
class RagdollParts
{
public:
    virtual ~RagdollParts() = default;

    /// The parts of @p ragdoll on @p skeleton, whose mesh has @p jointBounds (MeshData::jointBounds); empty
    /// when its profile cannot be read. The span stays valid until the next call.
    [[nodiscard]] virtual std::span<const RagdollPart> parts(const Ragdoll& ragdoll, const Skeleton& skeleton,
                                                             std::span<const Aabb> jointBounds) = 0;
};

/// What recordVisualisers() draws.
struct VisualiserOptions
{
    ViewportVisualisers kinds;       ///< The visualiser kinds.
    bool all = false;                ///< Every entity; otherwise those whose bit is set in @ref selection.
    std::span<const u64> selection;  ///< Entities to draw without @ref all (64 Entity indices per word).
    std::span<const u64> hidden;     ///< Entities never drawn (64 Entity indices per word).
    f32 cameraAspect = 16.0f / 9.0f; ///< Width / height of camera frusta.
    f32 frustumLength = 3.0f;        ///< Depth to which camera frusta are drawn, in world units.
    Entity previewCamera{};          ///< The VirtualCamera a Game view previews, drawn like a live one.
    f32 lineWidth = 1.5f;            ///< Line width in output pixels.
    VisualiserColors colors;         ///< Colours.
    /// The poses the skeleton kind draws (AnimationWorld::jointMatrices()); none draws no skeletons.
    const AnimationWorld* animation = nullptr;
    RagdollParts* ragdolls = nullptr; ///< Ragdoll profiles of the skeleton kind; none draws no ragdolls.
};

/// What recordVisualisers() recorded.
struct VisualiserStats
{
    u32 objects = 0;   ///< Visualisers recorded (an entity with a light and a collider counts twice).
    bool full = false; ///< The DebugDraw ran out of room; later visualisers are missing.
};

/// Records the visualisers of the entities of @p world that are not Inactive or hidden into @p draw (it is
/// not cleared). Positions come from the world-space Transforms, skeletons from the poses of
/// VisualiserOptions::animation; a TwoBoneIk on a child of an Animator is drawn with that Animator. Recording
/// stops when @p draw is full.
VisualiserStats recordVisualisers(World& world, DebugDraw& draw, const VisualiserOptions& options) noexcept;

} // namespace illusion::editor::viewport
