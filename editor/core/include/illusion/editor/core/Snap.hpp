#pragma once

/// @file
/// GPU-free snapping and measuring of the transform tools: the vertex snap grid of a mesh (V), the surface
/// under a pixel from a depth readback (surface snap), turning entities onto a surface normal, constraining a
/// snapped offset to the dragged handle, and the ruler's numbers.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/editor/core/Gizmo.hpp>

#include <optional>
#include <span>
#include <vector>

namespace illusion::editor
{

/// Distance in display points within which a vertex snap grabs a vertex.
inline constexpr f32 kVertexSnapTolerance = 12.0f;
/// Smallest vertex snap tolerance in pixels (`viewport.snap`).
inline constexpr f32 kMinSnapTolerance = 1.0f;
/// Largest vertex snap tolerance in pixels (`viewport.snap`).
inline constexpr f32 kMaxSnapTolerance = 64.0f;

/// What a vertex snap looks for: the vertex that appears nearest to a pixel, within a tolerance.
struct VertexSnapQuery
{
    Vec3 origin{0.0f};                 ///< Start of the world-space ray through the pixel.
    Vec3 direction{0.0f, 0.0f, -1.0f}; ///< Unit direction of the ray.
    Vec3 forward{0.0f, 0.0f, -1.0f};   ///< Unit view direction of the camera.
    /// A vertex within the tolerance lies at most `radius + slope * z` world units from the ray, where z is
    /// its depth along `forward` from `origin`: `radius` for parallel projections, `slope` for perspective.
    f32 radius = 0.0f;
    f32 slope = 0.0f;          ///< See radius.
    Mat4 viewProjection{1.0f}; ///< World to clip of the image (reversed-Z).
    UVec2 size{0};             ///< Image size in pixels.
    Vec2 pixel{0.0f};          ///< The pixel (origin at the top left, y down).
    f32 tolerance = 0.0f;      ///< Largest distance on screen in pixels.
};

/// The query for the vertex nearest to @p pixel of @p camera within @p tolerance pixels.
[[nodiscard]] VertexSnapQuery vertexSnapQuery(const GizmoCamera& camera, Vec2 pixel, f32 tolerance) noexcept;

/// A vertex that a VertexSnapQuery found.
struct VertexSnapHit
{
    Vec3 position{0.0f}; ///< World position.
    f32 distance = 0.0f; ///< Distance from the pixel on screen in pixels.
    f32 depth = 0.0f;    ///< Reversed-Z depth (larger is nearer).

    /// This hit is nearer to the pixel than @p other, or as near and nearer to the camera.
    [[nodiscard]] bool closerThan(const VertexSnapHit& other) const noexcept
    {
        return distance < other.distance || (distance == other.distance && depth > other.depth);
    }
};

/// The vertices of a mesh in a uniform grid of cells over their bounds (in the mesh's own space), so that a
/// vertex snap visits only the cells along the cursor ray. Built once per mesh; queries do not allocate.
class VertexSnapGrid
{
public:
    /// Vertices a cell holds on average when the mesh is spread evenly.
    static constexpr u32 kVerticesPerCell = 8;
    /// Most cells along each axis.
    static constexpr u32 kMaxCellsPerAxis = 64;

    /// An empty grid.
    VertexSnapGrid() = default;

    /// The grid of @p positions in the mesh's own space; equal positions are kept once and non-finite ones
    /// are skipped.
    explicit VertexSnapGrid(std::span<const Vec3> positions);

    /// The vertex that appears nearest to the pixel of @p query within its tolerance, with the mesh placed by
    /// @p toWorld, and the nearest to the camera among equally near ones; std::nullopt when no vertex in
    /// front of the camera is within the tolerance.
    [[nodiscard]] std::optional<VertexSnapHit> nearest(const VertexSnapQuery& query,
                                                       const Mat4& toWorld) const noexcept;

    /// Distinct vertices.
    [[nodiscard]] usize vertexCount() const noexcept { return m_positions.size(); }
    /// Cells along each axis (0 for an empty grid).
    [[nodiscard]] UVec3 cells() const noexcept { return m_cells; }
    /// Bounds of the vertices.
    [[nodiscard]] const Aabb& bounds() const noexcept { return m_bounds; }

private:
    [[nodiscard]] UVec3 cellOf(Vec3 position) const noexcept;
    [[nodiscard]] u32 cellIndex(UVec3 cell) const noexcept
    {
        return (cell.z * m_cells.y + cell.y) * m_cells.x + cell.x;
    }

    std::vector<Vec3> m_positions; // sorted by cell
    std::vector<u32> m_cellStart;  // first position of every cell, then the end
    Aabb m_bounds;
    Vec3 m_cellSize{1.0f};
    UVec3 m_cells{0};
};

/// A surface under a pixel.
struct SurfaceHit
{
    Vec3 position{0.0f}; ///< World position.
    Vec3 normal{0.0f};   ///< Unit normal facing the camera.
    Entity entity{};     ///< The entity drawn there (null when the readback had no ids).
};

/// A region of a depth readback: reversed-Z depth (0 where nothing was drawn) and the entity of every texel,
/// rows top first, of an image of `size` pixels rendered with `viewProjection`.
struct DepthPatch
{
    Mat4 viewProjection{1.0f};        ///< World to clip of the image.
    UVec2 size{0};                    ///< Image size in pixels.
    UVec2 origin{0};                  ///< Top left texel of the region.
    UVec2 extent{0};                  ///< Size of the region.
    std::span<const f32> depth;       ///< extent.x * extent.y depths.
    std::span<const Entity> entities; ///< The same number of entities, or empty.
};

/// The surface at @p texel (image pixels) of @p patch: its position from the depth, and its normal from the
/// neighbours of the same entity whose depth differs least on each side. std::nullopt where nothing was drawn
/// or when a neighbour is missing along x or y.
[[nodiscard]] std::optional<SurfaceHit> surfaceFromDepth(const DepthPatch& patch, UVec2 texel) noexcept;

/// @p rotation turned by the shortest arc that takes its +y axis onto @p normal (a unit vector); about its x
/// axis when the normal points the other way.
[[nodiscard]] Quat alignToNormal(Quat rotation, Vec3 normal) noexcept;

/// Bits of the axes of the gizmo's pose that a Translate @p handle moves along (bit i: axis i): one for an
/// axis, two for a plane, all three for TranslateScreen; 0 for the other handles.
[[nodiscard]] u32 gizmoHandleAxes(GizmoHandle handle) noexcept;

/// @p offset restricted to the axes of @p pose in @p axes (bit i: axis i): its projection onto that line or
/// plane, unchanged for 0 or all three axes.
[[nodiscard]] Vec3 constrainOffset(const GizmoPose& pose, u32 axes, Vec3 offset) noexcept;

/// The numbers of a ruler from one point to another (`gizmo.measure`).
struct Measurement
{
    Vec3 from{0.0f};       ///< Start.
    Vec3 to{0.0f};         ///< End.
    Vec3 delta{0.0f};      ///< to - from.
    f32 distance = 0.0f;   ///< Length of delta.
    f32 horizontal = 0.0f; ///< Length of delta in the ground plane (x and z).
    f32 elevation = 0.0f;  ///< Degrees above the ground plane, -90 to 90 (0 for a zero delta).
};

/// The ruler from @p from to @p to.
[[nodiscard]] Measurement measure(Vec3 from, Vec3 to) noexcept;

} // namespace illusion::editor
