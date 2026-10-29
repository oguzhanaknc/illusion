#pragma once

/// @file
/// The DebugDraw visualisers of the Scene views (GPU-free): light ranges and directions, camera frusta,
/// collider shapes, character capsules, joint anchors, axes and limits, and audio distances and cones,
/// recorded as world-space lines.
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
#include <illusion/editor/core/ViewportHost.hpp>

#include <span>

namespace illusion
{
class DebugDraw;
class World;
} // namespace illusion

namespace illusion::editor::viewport
{

/// Linear RGBA colours of the visualisers.
struct VisualiserColors
{
    Vec4 light{1.0f, 0.8f, 0.25f, 1.0f};       ///< Light ranges and directions.
    Vec4 camera{0.8f, 0.8f, 0.8f, 1.0f};       ///< Camera frusta.
    Vec4 collider{0.25f, 0.9f, 0.3f, 1.0f};    ///< Solid colliders.
    Vec4 sensor{0.3f, 0.55f, 1.0f, 1.0f};      ///< Colliders of sensor bodies (triggers).
    Vec4 character{0.2f, 0.85f, 0.85f, 1.0f};  ///< Character capsules.
    Vec4 joint{1.0f, 0.35f, 0.75f, 1.0f};      ///< Joint anchors and axes.
    Vec4 jointLimit{1.0f, 0.55f, 0.15f, 1.0f}; ///< Joint limits.
    Vec4 audioNear{0.45f, 0.6f, 1.0f, 1.0f};   ///< AudioSource minimum distance and inner cone.
    Vec4 audioFar{0.25f, 0.3f, 0.75f, 1.0f};   ///< AudioSource maximum distance and outer cone.
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
    f32 lineWidth = 1.5f;            ///< Line width in output pixels.
    VisualiserColors colors;         ///< Colours.
};

/// What recordVisualisers() recorded.
struct VisualiserStats
{
    u32 objects = 0;   ///< Visualisers recorded (an entity with a light and a collider counts twice).
    bool full = false; ///< The DebugDraw ran out of room; later visualisers are missing.
};

/// Records the visualisers of the entities of @p world that are not Inactive or hidden into @p draw (it is
/// not cleared). Positions come from the world-space Transforms. Recording stops when @p draw is full.
VisualiserStats recordVisualisers(World& world, DebugDraw& draw, const VisualiserOptions& options) noexcept;

} // namespace illusion::editor::viewport
