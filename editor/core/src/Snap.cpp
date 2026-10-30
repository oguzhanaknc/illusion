#include <illusion/core/Profiler.hpp>
#include <illusion/editor/core/Snap.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace illusion::editor
{
namespace
{

bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool lexicographicLess(Vec3 a, Vec3 b) noexcept
{
    if (a.x != b.x)
    {
        return a.x < b.x;
    }
    if (a.y != b.y)
    {
        return a.y < b.y;
    }
    return a.z < b.z;
}

std::array<Vec3, 8> cornersOf(const Aabb& box) noexcept
{
    std::array<Vec3, 8> out{};
    for (u32 i = 0; i < 8; ++i)
    {
        out[i] = Vec3((i & 1u) != 0 ? box.max.x : box.min.x, (i & 2u) != 0 ? box.max.y : box.min.y,
                      (i & 4u) != 0 ? box.max.z : box.min.z);
    }
    return out;
}

// Cell range [low, high] of the grid coordinates [from, to] along an axis of @p cells cells; false when it
// lies outside the grid.
bool cellRange(f32 from, f32 to, u32 cells, u32& low, u32& high) noexcept
{
    const f32 last = static_cast<f32>(cells - 1);
    if (!(to >= 0.0f) || !(from <= static_cast<f32>(cells)))
    {
        return false;
    }
    low = static_cast<u32>(std::clamp(std::floor(from), 0.0f, last));
    high = static_cast<u32>(std::clamp(std::floor(to), 0.0f, last));
    return true;
}

} // namespace

VertexSnapQuery vertexSnapQuery(const GizmoCamera& camera, Vec2 pixel, f32 tolerance) noexcept
{
    VertexSnapQuery query;
    const GizmoRay ray = gizmoRay(camera, pixel);
    query.origin = ray.origin;
    query.direction = ray.direction;
    query.forward = camera.rotation * Vec3(0.0f, 0.0f, -1.0f);
    const Mat4 view =
        glm::mat4_cast(glm::conjugate(camera.rotation)) * glm::translate(Mat4(1.0f), -camera.eye);
    query.viewProjection = camera.projection * view;
    query.size = camera.size;
    query.pixel = pixel;
    query.tolerance = std::max(tolerance, 0.0f);
    // World units per pixel: at a depth of 1 for perspective views, everywhere for parallel ones.
    const f32 scale = std::abs(camera.projection[1][1]) * static_cast<f32>(std::max(camera.size.y, 1u));
    const f32 reach = scale > 1e-12f ? query.tolerance * 2.0f / scale : 0.0f;
    (camera.orthographic ? query.radius : query.slope) = reach;
    return query;
}

VertexSnapGrid::VertexSnapGrid(std::span<const Vec3> positions)
{
    std::vector<Vec3> points;
    points.reserve(positions.size());
    for (const Vec3 position : positions)
    {
        if (finite(position))
        {
            points.push_back(position);
            m_bounds.expand(position);
        }
    }
    if (points.empty())
    {
        return;
    }
    // Cells of about the same size along each axis, kVerticesPerCell vertices each on average; flat axes get
    // one.
    const Vec3 extent = m_bounds.max - m_bounds.min;
    const usize target = std::max<usize>(1, points.size() / kVerticesPerCell);
    f32 volume = 1.0f;
    u32 dimensions = 0;
    for (u32 axis = 0; axis < 3; ++axis)
    {
        if (extent[static_cast<glm::length_t>(axis)] > 0.0f)
        {
            volume *= extent[static_cast<glm::length_t>(axis)];
            ++dimensions;
        }
    }
    const f32 edge = dimensions == 0
                         ? 1.0f
                         : std::pow(volume / static_cast<f32>(target), 1.0f / static_cast<f32>(dimensions));
    for (u32 axis = 0; axis < 3; ++axis)
    {
        const auto i = static_cast<glm::length_t>(axis);
        if (extent[i] > 0.0f && edge > 0.0f && std::isfinite(edge))
        {
            const f32 count =
                std::clamp(std::ceil(extent[i] / edge), 1.0f, static_cast<f32>(kMaxCellsPerAxis));
            m_cells[i] = static_cast<u32>(count);
            m_cellSize[i] = extent[i] / count;
        }
        else
        {
            m_cells[i] = 1;
            m_cellSize[i] = 1.0f;
        }
    }
    const usize cellCount = static_cast<usize>(m_cells.x) * m_cells.y * m_cells.z;
    std::vector<u32> cellOfPoint(points.size());
    std::vector<u32> start(cellCount + 1, 0);
    for (usize i = 0; i < points.size(); ++i)
    {
        cellOfPoint[i] = cellIndex(cellOf(points[i]));
        ++start[cellOfPoint[i] + 1];
    }
    for (usize cell = 0; cell < cellCount; ++cell)
    {
        start[cell + 1] += start[cell];
    }
    std::vector<Vec3> sorted(points.size());
    std::vector<u32> next(start.begin(), start.end() - 1);
    for (usize i = 0; i < points.size(); ++i)
    {
        sorted[next[cellOfPoint[i]]++] = points[i];
    }
    // Equal positions (vertices split for normals or UVs) are kept once.
    m_positions.reserve(sorted.size());
    m_cellStart.resize(cellCount + 1);
    for (usize cell = 0; cell < cellCount; ++cell)
    {
        m_cellStart[cell] = static_cast<u32>(m_positions.size());
        const auto first = sorted.begin() + start[cell];
        const auto last = sorted.begin() + start[cell + 1];
        std::sort(first, last, lexicographicLess);
        const auto end = std::unique(first, last);
        m_positions.insert(m_positions.end(), first, end);
    }
    m_cellStart[cellCount] = static_cast<u32>(m_positions.size());
    m_positions.shrink_to_fit();
}

UVec3 VertexSnapGrid::cellOf(Vec3 position) const noexcept
{
    UVec3 out{0};
    for (u32 axis = 0; axis < 3; ++axis)
    {
        const auto i = static_cast<glm::length_t>(axis);
        const f32 cell = std::floor((position[i] - m_bounds.min[i]) / m_cellSize[i]);
        out[i] = static_cast<u32>(std::clamp(cell, 0.0f, static_cast<f32>(m_cells[i] - 1)));
    }
    return out;
}

std::optional<VertexSnapHit> VertexSnapGrid::nearest(const VertexSnapQuery& query,
                                                     const Mat4& toWorld) const noexcept
{
    ILL_PROFILE_SCOPE();
    if (m_positions.empty() || query.size.x == 0 || query.size.y == 0)
    {
        return std::nullopt;
    }
    const Mat4 toLocal = glm::inverse(toWorld);
    const Mat3 inverse3(toLocal);
    // The Frobenius norm bounds how much the inverse stretches a distance.
    f32 stretch = 0.0f;
    for (u32 column = 0; column < 3; ++column)
    {
        stretch += glm::dot(inverse3[static_cast<glm::length_t>(column)],
                            inverse3[static_cast<glm::length_t>(column)]);
    }
    stretch = std::sqrt(stretch);
    const Vec3 origin = Vec3(toLocal * Vec4(query.origin, 1.0f));
    const Vec3 direction = inverse3 * query.direction;
    const f32 lengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(stretch) || !finite(origin) || !(lengthSquared > 0.0f) ||
        !std::isfinite(lengthSquared))
    {
        return std::nullopt;
    }
    // The part of the ray where the nearest point to a vertex can lie, and how far from it the vertex can be.
    const bool perspective = query.slope > 0.0f;
    f32 near = perspective ? 0.0f : std::numeric_limits<f32>::max();
    f32 far = std::numeric_limits<f32>::lowest();
    f32 depth = 0.0f;
    // In world units along the ray (the same parameter in both spaces): the nearest point to a world position
    // is linear in it, so the corners of the box bound it.
    for (const Vec3 corner : cornersOf(m_bounds))
    {
        const Vec3 world = Vec3(toWorld * Vec4(corner, 1.0f));
        const f32 along = glm::dot(world - query.origin, query.direction);
        far = std::max(far, along);
        if (!perspective)
        {
            near = std::min(near, along);
        }
        depth = std::max(depth, glm::dot(world - query.origin, query.forward));
    }
    const f32 reach = (query.radius + query.slope * depth) * stretch;
    if (!(far >= near) || !std::isfinite(reach))
    {
        return std::nullopt;
    }
    for (u32 axis = 0; axis < 3; ++axis)
    {
        const auto i = static_cast<glm::length_t>(axis);
        const f32 low = m_bounds.min[i] - reach;
        const f32 high = m_bounds.max[i] + reach;
        if (std::abs(direction[i]) < 1e-20f)
        {
            if (origin[i] < low || origin[i] > high)
            {
                return std::nullopt;
            }
            continue;
        }
        f32 enter = (low - origin[i]) / direction[i];
        f32 exit = (high - origin[i]) / direction[i];
        if (enter > exit)
        {
            std::swap(enter, exit);
        }
        near = std::max(near, enter);
        far = std::min(far, exit);
    }
    if (!(far >= near))
    {
        return std::nullopt;
    }

    // Grid coordinates along the segment and the reach in cells. The cells are visited in slices across the
    // axis the segment runs along most, each slice once: the cells within reach of the part of the segment
    // that is within reach of the slice.
    const Vec3 start = (origin + direction * near - m_bounds.min) / m_cellSize;
    const Vec3 end = (origin + direction * far - m_bounds.min) / m_cellSize;
    const Vec3 cellReach = Vec3(reach) / m_cellSize;
    const Vec3 span = end - start;
    glm::length_t major = 0;
    for (glm::length_t i = 1; i < 3; ++i)
    {
        if (std::abs(span[i]) > std::abs(span[major]))
        {
            major = i;
        }
    }
    const glm::length_t minorA = (major + 1) % 3;
    const glm::length_t minorB = (major + 2) % 3;
    u32 firstSlice = 0;
    u32 lastSlice = 0;
    if (!cellRange(std::min(start[major], end[major]) - cellReach[major],
                   std::max(start[major], end[major]) + cellReach[major], m_cells[major], firstSlice,
                   lastSlice))
    {
        return std::nullopt;
    }
    const f32 width = static_cast<f32>(query.size.x);
    const f32 height = static_cast<f32>(query.size.y);
    // Most vertices of the visited cells are farther from the ray than the reach: a cheap test in the mesh's
    // own space skips them before the exact one on screen (with a margin for rounding).
    const f32 limit = reach * 1.01f + 1e-6f;
    const f32 limitSquared = limit * limit;
    const Vec3 unit = direction / lengthSquared;
    std::optional<VertexSnapHit> best;
    for (u32 slice = firstSlice; slice <= lastSlice; ++slice)
    {
        // The part of the segment (as fractions 0 to 1) within reach of this slice.
        f32 from = 0.0f;
        f32 to = 1.0f;
        if (std::abs(span[major]) > 1e-12f)
        {
            from = (static_cast<f32>(slice) - cellReach[major] - start[major]) / span[major];
            to = (static_cast<f32>(slice + 1) + cellReach[major] - start[major]) / span[major];
            if (from > to)
            {
                std::swap(from, to);
            }
            from = std::max(from, 0.0f);
            to = std::min(to, 1.0f);
            if (from > to)
            {
                continue;
            }
        }
        const Vec3 a = start + span * from;
        const Vec3 b = start + span * to;
        UVec3 low{0};
        UVec3 high{0};
        low[major] = slice;
        high[major] = slice;
        if (!cellRange(std::min(a[minorA], b[minorA]) - cellReach[minorA],
                       std::max(a[minorA], b[minorA]) + cellReach[minorA], m_cells[minorA], low[minorA],
                       high[minorA]) ||
            !cellRange(std::min(a[minorB], b[minorB]) - cellReach[minorB],
                       std::max(a[minorB], b[minorB]) + cellReach[minorB], m_cells[minorB], low[minorB],
                       high[minorB]))
        {
            continue;
        }
        UVec3 cell{0};
        for (cell.z = low.z; cell.z <= high.z; ++cell.z)
        {
            for (cell.y = low.y; cell.y <= high.y; ++cell.y)
            {
                for (cell.x = low.x; cell.x <= high.x; ++cell.x)
                {
                    const u32 index = cellIndex(cell);
                    for (u32 k = m_cellStart[index]; k < m_cellStart[index + 1]; ++k)
                    {
                        const Vec3 offset = m_positions[k] - origin;
                        const Vec3 across = offset - direction * glm::dot(offset, unit);
                        if (glm::dot(across, across) > limitSquared)
                        {
                            continue;
                        }
                        const Vec3 world = Vec3(toWorld * Vec4(m_positions[k], 1.0f));
                        const Vec4 clip = query.viewProjection * Vec4(world, 1.0f);
                        if (!(clip.w > 0.0f))
                        {
                            continue;
                        }
                        const Vec3 ndc = Vec3(clip) / clip.w;
                        if (!(ndc.z >= 0.0f && ndc.z <= 1.0f))
                        {
                            continue;
                        }
                        const Vec2 pixel((ndc.x * 0.5f + 0.5f) * width, (0.5f - ndc.y * 0.5f) * height);
                        const f32 distance = glm::length(pixel - query.pixel);
                        if (!(distance <= query.tolerance))
                        {
                            continue;
                        }
                        const VertexSnapHit hit{world, distance, ndc.z};
                        if (!best || hit.closerThan(*best))
                        {
                            best = hit;
                        }
                    }
                }
            }
        }
    }
    return best;
}

std::optional<SurfaceHit> surfaceFromDepth(const DepthPatch& patch, UVec2 texel) noexcept
{
    const usize count = static_cast<usize>(patch.extent.x) * patch.extent.y;
    if (count == 0 || patch.size.x == 0 || patch.size.y == 0 || patch.depth.size() < count ||
        (!patch.entities.empty() && patch.entities.size() < count) || texel.x < patch.origin.x ||
        texel.y < patch.origin.y || texel.x - patch.origin.x >= patch.extent.x ||
        texel.y - patch.origin.y >= patch.extent.y)
    {
        return std::nullopt;
    }
    // Double precision: reversed-Z depths of distant surfaces are small, and the inverse divides by them.
    const glm::dmat4 inverse = glm::inverse(glm::dmat4(patch.viewProjection));
    const auto indexOf = [&](UVec2 at) {
        return static_cast<usize>(at.y - patch.origin.y) * patch.extent.x + (at.x - patch.origin.x);
    };
    const auto entityAt = [&](UVec2 at) {
        return patch.entities.empty() ? Entity{} : patch.entities[indexOf(at)];
    };
    const auto unproject = [&](UVec2 at, f32 depth) {
        const glm::dvec2 ndc((static_cast<f64>(at.x) + 0.5) / static_cast<f64>(patch.size.x) * 2.0 - 1.0,
                             1.0 - (static_cast<f64>(at.y) + 0.5) / static_cast<f64>(patch.size.y) * 2.0);
        const glm::dvec4 world = inverse * glm::dvec4(ndc, static_cast<f64>(depth), 1.0);
        return glm::dvec3(world) / world.w;
    };
    const f32 depth = patch.depth[indexOf(texel)];
    if (!(depth > 0.0f) || !(depth <= 1.0f))
    {
        return std::nullopt;
    }
    const Entity entity = entityAt(texel);
    const glm::dvec3 position = unproject(texel, depth);
    // Towards +x or +y on screen from the neighbour on the side whose depth differs least.
    const auto tangent = [&](i32 dx, i32 dy) -> std::optional<glm::dvec3> {
        std::optional<glm::dvec3> out;
        f32 difference = std::numeric_limits<f32>::max();
        for (const i32 sign : {-1, 1})
        {
            const i64 x = static_cast<i64>(texel.x) + sign * dx;
            const i64 y = static_cast<i64>(texel.y) + sign * dy;
            if (x < patch.origin.x || y < patch.origin.y || x >= i64{patch.origin.x} + patch.extent.x ||
                y >= i64{patch.origin.y} + patch.extent.y)
            {
                continue;
            }
            const UVec2 at(static_cast<u32>(x), static_cast<u32>(y));
            const f32 other = patch.depth[indexOf(at)];
            if (!(other > 0.0f) || !(other <= 1.0f) || entityAt(at) != entity ||
                !(std::abs(other - depth) < difference))
            {
                continue;
            }
            difference = std::abs(other - depth);
            out = (unproject(at, other) - position) * static_cast<f64>(sign);
        }
        return out;
    };
    const std::optional<glm::dvec3> alongX = tangent(1, 0);
    const std::optional<glm::dvec3> alongY = tangent(0, 1);
    if (!alongX || !alongY)
    {
        return std::nullopt;
    }
    glm::dvec3 normal = glm::cross(*alongX, *alongY);
    const f64 length = glm::length(normal);
    if (!(length > 0.0) || !std::isfinite(length) || !finite(Vec3(position)))
    {
        return std::nullopt;
    }
    normal /= length;
    // Facing the camera: against the direction from the near plane to the surface.
    if (glm::dot(normal, position - unproject(texel, 1.0f)) > 0.0)
    {
        normal = -normal;
    }
    return SurfaceHit{Vec3(position), Vec3(normal), entity};
}

Quat alignToNormal(Quat rotation, Vec3 normal) noexcept
{
    const f32 length = glm::length(normal);
    if (!(length > 0.0f) || !std::isfinite(length))
    {
        return rotation;
    }
    normal /= length;
    const Vec3 up = rotation * Vec3(0.0f, 1.0f, 0.0f);
    const f32 cosine = glm::dot(up, normal);
    if (cosine < -0.99999f)
    {
        return glm::normalize(glm::angleAxis(std::numbers::pi_v<f32>, rotation * Vec3(1.0f, 0.0f, 0.0f)) *
                              rotation);
    }
    const Vec3 axis = glm::cross(up, normal);
    return glm::normalize(Quat(1.0f + cosine, axis.x, axis.y, axis.z) * rotation);
}

u32 gizmoHandleAxes(GizmoHandle handle) noexcept
{
    switch (handle)
    {
    case GizmoHandle::TranslateX:
        return 0b001u;
    case GizmoHandle::TranslateY:
        return 0b010u;
    case GizmoHandle::TranslateZ:
        return 0b100u;
    case GizmoHandle::TranslateYZ:
        return 0b110u;
    case GizmoHandle::TranslateXZ:
        return 0b101u;
    case GizmoHandle::TranslateXY:
        return 0b011u;
    case GizmoHandle::TranslateScreen:
        return 0b111u;
    default:
        return 0;
    }
}

Vec3 constrainOffset(const GizmoPose& pose, u32 axes, Vec3 offset) noexcept
{
    axes &= 0b111u;
    const u32 count = (axes & 1u) + ((axes >> 1u) & 1u) + ((axes >> 2u) & 1u);
    if (count == 0 || count == 3)
    {
        return offset;
    }
    std::array<Vec3, 3> basis{};
    for (u32 i = 0; i < 3; ++i)
    {
        Vec3 unit(0.0f);
        unit[static_cast<glm::length_t>(i)] = 1.0f;
        basis[i] = pose.orientation * unit;
    }
    if (count == 1)
    {
        const Vec3 axis = basis[axes == 1u ? 0 : axes == 2u ? 1 : 2];
        return axis * glm::dot(offset, axis);
    }
    const Vec3 normal = basis[(axes & 1u) == 0 ? 0 : (axes & 2u) == 0 ? 1 : 2];
    return offset - normal * glm::dot(offset, normal);
}

Measurement measure(Vec3 from, Vec3 to) noexcept
{
    Measurement out;
    out.from = from;
    out.to = to;
    out.delta = to - from;
    out.distance = glm::length(out.delta);
    out.horizontal = glm::length(Vec2(out.delta.x, out.delta.z));
    out.elevation = out.distance > 0.0f ? glm::degrees(std::atan2(out.delta.y, out.horizontal)) : 0.0f;
    return out;
}

} // namespace illusion::editor
