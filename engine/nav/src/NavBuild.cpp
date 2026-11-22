#include "NavAllocator.hpp"
#include "NavBake.hpp"
#include "NavValidation.hpp"

#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavBuild.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>

namespace illusion
{
namespace
{

constexpr u32 kSegments = 16;       // around the axis of spheres, capsules and cylinders
constexpr u32 kHalfRings = 4;       // from a pole to the equator of spheres and capsules
constexpr f32 kDegenerate = 1e-12f; // squared length of the normal of a triangle without area

// --- Bake input ---------------------------------------------------------------------------------------

// Appends the triangles of one source to a NavGeometry, in world space, oriented and filtered by a box.
class TriangleSink
{
public:
    TriangleSink(NavGeometry& geometry, const Aabb& filter) noexcept
        : m_geometry(&geometry)
        , m_filter(filter)
    {
    }

    void begin(const Mat4& matrix, u8 area) noexcept
    {
        m_matrix = matrix;
        m_area = area;
        m_center = Vec3(matrix[3]);
        m_mirrored = glm::determinant(Mat3(matrix)) < 0.0f;
    }

    // A triangle of a convex shape around its local origin: turned to face away from it.
    void addConvex(const Vec3& a, const Vec3& b, const Vec3& c)
    {
        const Vec3 wa = point(a);
        const Vec3 wb = point(b);
        const Vec3 wc = point(c);
        const Vec3 normal = glm::cross(wb - wa, wc - wa);
        if (glm::dot(normal, normal) <= kDegenerate)
        {
            return;
        }
        if (glm::dot(normal, (wa + wb + wc) / 3.0f - m_center) < 0.0f)
        {
            append(wa, wc, wb);
        }
        else
        {
            append(wa, wb, wc);
        }
    }

    // A triangle of a mesh, counter-clockwise from its front.
    void addMesh(const Vec3& a, const Vec3& b, const Vec3& c)
    {
        if (m_mirrored)
        {
            append(point(a), point(c), point(b));
        }
        else
        {
            append(point(a), point(b), point(c));
        }
    }

private:
    [[nodiscard]] Vec3 point(const Vec3& local) const noexcept { return Vec3(m_matrix * Vec4(local, 1.0f)); }

    void append(const Vec3& a, const Vec3& b, const Vec3& c)
    {
        const Vec3 low = glm::min(a, glm::min(b, c));
        const Vec3 high = glm::max(a, glm::max(b, c));
        if (high.x < m_filter.min.x || low.x > m_filter.max.x || high.y < m_filter.min.y ||
            low.y > m_filter.max.y || high.z < m_filter.min.z || low.z > m_filter.max.z)
        {
            return;
        }
        const u32 first = static_cast<u32>(m_geometry->vertices.size());
        m_geometry->vertices.insert(m_geometry->vertices.end(), {a, b, c});
        m_geometry->indices.insert(m_geometry->indices.end(), {first, first + 1, first + 2});
        m_geometry->areas.push_back(m_area);
    }

    NavGeometry* m_geometry;
    Aabb m_filter;
    Mat4 m_matrix{1.0f};
    Vec3 m_center{0.0f};
    u8 m_area = 0;
    bool m_mirrored = false;
};

void addBox(TriangleSink& sink, const Vec3& half)
{
    // Each face: its normal axis and two axes of the face, as unit vectors scaled by the half extents.
    const Vec3 x(half.x, 0.0f, 0.0f);
    const Vec3 y(0.0f, half.y, 0.0f);
    const Vec3 z(0.0f, 0.0f, half.z);
    const Vec3 faces[6][3] = {{x, y, z}, {-x, z, y}, {y, z, x}, {-y, x, z}, {z, x, y}, {-z, y, x}};
    for (const auto& face : faces)
    {
        const Vec3& n = face[0];
        const Vec3& u = face[1];
        const Vec3& v = face[2];
        sink.addConvex(n - u - v, n + u - v, n + u + v);
        sink.addConvex(n - u - v, n + u + v, n - u + v);
    }
}

// A sphere (halfHeight 0) or a capsule along +y: two hemispheres around (0, ±halfHeight, 0) joined by a band.
void addRounded(TriangleSink& sink, f32 radius, f32 halfHeight)
{
    constexpr u32 kRows = 2 * (kHalfRings + 1);
    Vec3 rows[kRows][kSegments];
    for (u32 row = 0; row < kRows; ++row)
    {
        const u32 ring = row <= kHalfRings ? row : row - 1;
        const f32 polar = std::numbers::pi_v<f32> * static_cast<f32>(ring) / static_cast<f32>(2 * kHalfRings);
        const f32 offset = row <= kHalfRings ? halfHeight : -halfHeight;
        for (u32 segment = 0; segment < kSegments; ++segment)
        {
            const f32 azimuth =
                2.0f * std::numbers::pi_v<f32> * static_cast<f32>(segment) / static_cast<f32>(kSegments);
            rows[row][segment] =
                Vec3(radius * std::sin(polar) * std::cos(azimuth), radius * std::cos(polar) + offset,
                     radius * std::sin(polar) * std::sin(azimuth));
        }
    }
    for (u32 row = 0; row + 1 < kRows; ++row)
    {
        for (u32 segment = 0; segment < kSegments; ++segment)
        {
            const u32 next = (segment + 1) % kSegments;
            sink.addConvex(rows[row][segment], rows[row + 1][segment], rows[row + 1][next]);
            sink.addConvex(rows[row][segment], rows[row + 1][next], rows[row][next]);
        }
    }
}

void addCylinder(TriangleSink& sink, f32 radius, f32 halfHeight)
{
    const Vec3 top(0.0f, halfHeight, 0.0f);
    for (u32 segment = 0; segment < kSegments; ++segment)
    {
        const f32 a =
            2.0f * std::numbers::pi_v<f32> * static_cast<f32>(segment) / static_cast<f32>(kSegments);
        const f32 b =
            2.0f * std::numbers::pi_v<f32> * static_cast<f32>(segment + 1) / static_cast<f32>(kSegments);
        const Vec3 p(radius * std::cos(a), 0.0f, radius * std::sin(a));
        const Vec3 q(radius * std::cos(b), 0.0f, radius * std::sin(b));
        sink.addConvex(p + top, q + top, q - top);
        sink.addConvex(p + top, q - top, p - top);
        sink.addConvex(top, p + top, q + top);
        sink.addConvex(-top, q - top, p - top);
    }
}

void addMeshData(TriangleSink& sink, const MeshData& mesh)
{
    const std::span<const u32> indices = fullDetailIndices(mesh);
    const usize vertexCount = mesh.vertices.size();
    for (usize i = 0; i + 2 < indices.size(); i += 3)
    {
        if (indices[i] < vertexCount && indices[i + 1] < vertexCount && indices[i + 2] < vertexCount)
        {
            sink.addMesh(mesh.vertices[indices[i]].position, mesh.vertices[indices[i + 1]].position,
                         mesh.vertices[indices[i + 2]].position);
        }
    }
}

[[nodiscard]] f32 yawOf(const Quat& rotation) noexcept
{
    const Vec3 forward = rotation * Vec3(1.0f, 0.0f, 0.0f);
    return std::atan2(-forward.z, forward.x);
}

[[nodiscard]] Aabb volumeBounds(const NavVolumeData& volume) noexcept
{
    Vec3 half = volume.halfExtents;
    if (!volume.cylinder)
    {
        const f32 c = std::abs(std::cos(volume.yaw));
        const f32 s = std::abs(std::sin(volume.yaw));
        half = Vec3(c * half.x + s * half.z, half.y, s * half.x + c * half.z);
    }
    return Aabb{.min = volume.center - half, .max = volume.center + half};
}

[[nodiscard]] bool overlaps(const Aabb& a, const Aabb& b) noexcept
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

[[nodiscard]] bool insideXz(const Vec3& point, const Aabb& box) noexcept
{
    return point.x >= box.min.x && point.x <= box.max.x && point.z >= box.min.z && point.z <= box.max.z;
}

// Cells around a tile that its Recast build also reads (ceil(radius / cell) + 3, as Recast's samples do).
[[nodiscard]] i32 borderCells(const NavAgentProfile& agent, const NavBuildSettings& build) noexcept
{
    return static_cast<i32>(std::ceil(agent.radius / build.cellSize)) + 3;
}

// --- Tile grid ----------------------------------------------------------------------------------------

using TileGrid = detail::NavTileGrid;

[[nodiscard]] Expected<TileGrid, std::string> makeGrid(const NavGeometry& geometry)
{
    if (std::string error = detail::navSettingsError(geometry.agent, geometry.build); !error.empty())
    {
        return unexpected(std::move(error));
    }
    const Aabb& bounds = geometry.bounds;
    for (const Vec3& corner : {bounds.min, bounds.max})
    {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z))
        {
            return unexpected(std::string("the surface's box is not finite"));
        }
    }
    if (!bounds.isValid())
    {
        return unexpected(std::string("the surface's box is empty"));
    }
    if ((bounds.max.y - bounds.min.y) / geometry.build.cellHeight > detail::kMaxHeightCells)
    {
        return unexpected(std::string("size.y spans more than 8191 cells of build.cellHeight"));
    }
    TileGrid grid;
    grid.origin = bounds.min;
    grid.tileWorld = static_cast<f32>(geometry.build.tileSize) * geometry.build.cellSize;
    grid.borderCells = borderCells(geometry.agent, geometry.build);
    grid.border = static_cast<f32>(grid.borderCells) * geometry.build.cellSize;
    const f64 columns =
        std::ceil(static_cast<f64>(bounds.max.x - bounds.min.x) / static_cast<f64>(grid.tileWorld));
    const f64 rows =
        std::ceil(static_cast<f64>(bounds.max.z - bounds.min.z) / static_cast<f64>(grid.tileWorld));
    if (std::max(columns, 1.0) * std::max(rows, 1.0) > static_cast<f64>(detail::kMaxNavTiles))
    {
        return unexpected(std::string("the surface needs more than 1048576 tiles: raise build.tileSize"));
    }
    grid.tilesX = std::max(static_cast<u32>(columns), 1u);
    grid.tilesZ = std::max(static_cast<u32>(rows), 1u);

    const usize triangles = geometry.indices.size() / 3;
    if (geometry.indices.size() % 3 != 0 || geometry.areas.size() != triangles)
    {
        return unexpected(std::string("the geometry needs three indices and one area per triangle"));
    }
    for (const u32 index : geometry.indices)
    {
        if (index >= geometry.vertices.size())
        {
            return unexpected(std::string("an index of the geometry is out of range"));
        }
    }
    for (const Vec3& vertex : geometry.vertices)
    {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
        {
            return unexpected(std::string("a vertex of the geometry is not finite"));
        }
    }
    const auto validArea = [](u8 area) {
        return area < kMaxNavAreas || area == kNavAreaNotWalkable;
    };
    if (!std::ranges::all_of(geometry.areas, validArea) ||
        !std::ranges::all_of(geometry.volumes, [&](const NavVolumeData& v) { return validArea(v.area); }) ||
        !std::ranges::all_of(geometry.links, [](const NavLinkData& l) { return l.area < kMaxNavAreas; }))
    {
        return unexpected(std::string("an area of the geometry is out of range"));
    }
    return grid;
}

// The box a tile's build reads: the tile and its border, over the surface's height.
[[nodiscard]] Aabb tileBox(const NavGeometry& geometry, const TileGrid& grid, NavTileCoord tile) noexcept
{
    const f32 x0 = grid.origin.x + static_cast<f32>(tile.x) * grid.tileWorld;
    const f32 z0 = grid.origin.z + static_cast<f32>(tile.z) * grid.tileWorld;
    return Aabb{.min = Vec3(x0 - grid.border, geometry.bounds.min.y, z0 - grid.border),
                .max = Vec3(x0 + grid.tileWorld + grid.border, geometry.bounds.max.y,
                            z0 + grid.tileWorld + grid.border)};
}

[[nodiscard]] Aabb triangleBounds(const NavGeometry& geometry, usize triangle) noexcept
{
    const Vec3& a = geometry.vertices[geometry.indices[triangle * 3]];
    const Vec3& b = geometry.vertices[geometry.indices[triangle * 3 + 1]];
    const Vec3& c = geometry.vertices[geometry.indices[triangle * 3 + 2]];
    return Aabb{.min = glm::min(a, glm::min(b, c)), .max = glm::max(a, glm::max(b, c))};
}

// --- Tile input hash ----------------------------------------------------------------------------------

class HashWriter
{
public:
    template <class T>
    void add(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>);
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        m_bytes.insert(m_bytes.end(), bytes, bytes + sizeof(T));
    }

    void add(f32 value)
    {
        u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        add(bits);
    }

    void add(const Vec3& value)
    {
        add(value.x);
        add(value.y);
        add(value.z);
    }

    void add(bool value) { add(static_cast<u8>(value ? 1 : 0)); }

    [[nodiscard]] u64 hash() const noexcept { return xxhash64(m_bytes); }

private:
    std::vector<std::byte> m_bytes;
};

// The tile's input: its triangles (@p triangles, ascending), the volumes and links that reach it, the
// settings, the grid and its coordinates.
[[nodiscard]] u64 hashTileInput(const NavGeometry& geometry, const TileGrid& grid, NavTileCoord tile,
                                std::span<const u32> triangles)
{
    HashWriter writer;
    const NavAgentProfile& agent = geometry.agent;
    const NavBuildSettings& build = geometry.build;
    writer.add(kNavMeshFormatVersion);
    writer.add(agent.agentType);
    for (const f32 value :
         {agent.radius, agent.height, agent.maxClimb, agent.maxSlope, build.cellSize, build.cellHeight,
          build.minRegionArea, build.mergeRegionArea, build.maxEdgeLength, build.maxSimplificationError,
          build.detailSampleDistance, build.detailSampleMaxError})
    {
        writer.add(value);
    }
    writer.add(build.tileSize);
    writer.add(geometry.bounds.min);
    writer.add(geometry.bounds.max);
    writer.add(tile.x);
    writer.add(tile.z);
    for (const u32 triangle : triangles)
    {
        for (u32 corner = 0; corner < 3; ++corner)
        {
            writer.add(geometry.vertices[geometry.indices[triangle * 3 + corner]]);
        }
        writer.add(geometry.areas[triangle]);
    }
    const Aabb box = tileBox(geometry, grid, tile);
    for (const NavVolumeData& volume : geometry.volumes)
    {
        if (overlaps(volumeBounds(volume), box))
        {
            writer.add(volume.center);
            writer.add(volume.halfExtents);
            writer.add(volume.yaw);
            writer.add(volume.cylinder);
            writer.add(volume.area);
        }
    }
    for (usize i = 0; i < geometry.links.size(); ++i)
    {
        const NavLinkData& link = geometry.links[i];
        if (insideXz(link.start, box) || insideXz(link.end, box))
        {
            writer.add(static_cast<u32>(i));
            writer.add(link.start);
            writer.add(link.end);
            writer.add(link.radius);
            writer.add(link.area);
            writer.add(link.bidirectional);
        }
    }
    return writer.hash();
}

// Appends the triangles among @p candidates (ascending) that reach @p box.
void selectTriangles(const NavGeometry& geometry, const Aabb& box, std::span<const u32> candidates,
                     std::vector<u32>& out)
{
    for (const u32 triangle : candidates)
    {
        if (overlaps(triangleBounds(geometry, triangle), box))
        {
            out.push_back(triangle);
        }
    }
}

// --- Tile build ---------------------------------------------------------------------------------------

struct RecastDeleter
{
    void operator()(rcHeightfield* value) const noexcept { rcFreeHeightField(value); }
    void operator()(rcCompactHeightfield* value) const noexcept { rcFreeCompactHeightfield(value); }
    void operator()(rcContourSet* value) const noexcept { rcFreeContourSet(value); }
    void operator()(rcPolyMesh* value) const noexcept { rcFreePolyMesh(value); }
    void operator()(rcPolyMeshDetail* value) const noexcept { rcFreePolyMeshDetail(value); }
};

template <class T>
using RecastPtr = std::unique_ptr<T, RecastDeleter>;

using TileResult = detail::NavTileResult;

[[nodiscard]] u8 recastArea(u8 area) noexcept
{
    return area == kNavAreaNotWalkable ? u8{RC_NULL_AREA} : static_cast<u8>(area + 1);
}

void buildTile(const NavGeometry& geometry, const TileGrid& grid, NavTileCoord tile,
               std::span<const u32> candidates, TileResult& result)
{
    ILL_PROFILE_SCOPE();
    const Aabb box = tileBox(geometry, grid, tile);
    std::vector<u32> triangles;
    selectTriangles(geometry, box, candidates, triangles);
    result.inputHash = hashTileInput(geometry, grid, tile, triangles);
    if (triangles.empty())
    {
        return;
    }

    const NavAgentProfile& agent = geometry.agent;
    const NavBuildSettings& build = geometry.build;
    const f32 cs = build.cellSize;
    const f32 ch = build.cellHeight;
    const i32 walkableHeight = static_cast<i32>(std::ceil(agent.height / ch));
    const i32 walkableClimb = static_cast<i32>(std::floor(agent.maxClimb / ch));
    const i32 walkableRadius = static_cast<i32>(std::ceil(agent.radius / cs));
    const i32 size = static_cast<i32>(build.tileSize) + 2 * grid.borderCells;
    const f32 walkableNormalY = std::cos(agent.maxSlope);

    std::vector<f32> vertices;
    std::vector<u8> areas;
    vertices.reserve(triangles.size() * 9);
    areas.reserve(triangles.size());
    for (const u32 triangle : triangles)
    {
        Vec3 corners[3];
        for (u32 corner = 0; corner < 3; ++corner)
        {
            corners[corner] = geometry.vertices[geometry.indices[triangle * 3 + corner]];
            vertices.insert(vertices.end(), {corners[corner].x, corners[corner].y, corners[corner].z});
        }
        const Vec3 normal = glm::cross(corners[1] - corners[0], corners[2] - corners[0]);
        const f32 length = glm::length(normal);
        const bool walkable = length > 0.0f && normal.y > walkableNormalY * length;
        areas.push_back(walkable ? recastArea(geometry.areas[triangle]) : u8{RC_NULL_AREA});
    }

    const auto fail = [&](const char* step) {
        result.error =
            "tile (" + std::to_string(tile.x) + ", " + std::to_string(tile.z) + "): Recast could not " + step;
    };
    rcContext context(false);
    const f32 bmin[3] = {box.min.x, box.min.y, box.min.z};
    const f32 bmax[3] = {box.max.x, box.max.y, box.max.z};
    RecastPtr<rcHeightfield> solid(rcAllocHeightfield());
    if (!solid || !rcCreateHeightfield(&context, *solid, size, size, bmin, bmax, cs, ch))
    {
        fail("create the heightfield");
        return;
    }
    if (!rcRasterizeTriangles(&context, vertices.data(), areas.data(), static_cast<i32>(areas.size()), *solid,
                              walkableClimb))
    {
        fail("rasterize the triangles");
        return;
    }
    rcFilterLowHangingWalkableObstacles(&context, walkableClimb, *solid);
    rcFilterLedgeSpans(&context, walkableHeight, walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&context, walkableHeight, *solid);

    RecastPtr<rcCompactHeightfield> compact(rcAllocCompactHeightfield());
    if (!compact || !rcBuildCompactHeightfield(&context, walkableHeight, walkableClimb, *solid, *compact))
    {
        fail("build the compact heightfield");
        return;
    }
    solid.reset();
    if (!rcErodeWalkableArea(&context, walkableRadius, *compact))
    {
        fail("erode the walkable area");
        return;
    }
    // Areas first, then the cut-outs, so that a cut-out always wins.
    for (const bool cut : {false, true})
    {
        for (const NavVolumeData& volume : geometry.volumes)
        {
            if ((volume.area == kNavAreaNotWalkable) != cut || !overlaps(volumeBounds(volume), box))
            {
                continue;
            }
            const u8 area = recastArea(volume.area);
            if (volume.cylinder)
            {
                const f32 base[3] = {volume.center.x, volume.center.y - volume.halfExtents.y,
                                     volume.center.z};
                rcMarkCylinderArea(&context, base, volume.halfExtents.x, 2.0f * volume.halfExtents.y, area,
                                   *compact);
                continue;
            }
            const f32 c = std::cos(volume.yaw);
            const f32 s = std::sin(volume.yaw);
            f32 polygon[12];
            const f32 signs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
            for (u32 i = 0; i < 4; ++i)
            {
                const f32 lx = signs[i][0] * volume.halfExtents.x;
                const f32 lz = signs[i][1] * volume.halfExtents.z;
                polygon[i * 3] = volume.center.x + c * lx + s * lz;
                polygon[i * 3 + 1] = volume.center.y;
                polygon[i * 3 + 2] = volume.center.z - s * lx + c * lz;
            }
            rcMarkConvexPolyArea(&context, polygon, 4, volume.center.y - volume.halfExtents.y,
                                 volume.center.y + volume.halfExtents.y, area, *compact);
        }
    }
    const f32 cellArea = cs * cs;
    if (!rcBuildDistanceField(&context, *compact) ||
        !rcBuildRegions(&context, *compact, grid.borderCells,
                        static_cast<i32>(build.minRegionArea / cellArea),
                        static_cast<i32>(build.mergeRegionArea / cellArea)))
    {
        fail("build the regions");
        return;
    }
    RecastPtr<rcContourSet> contours(rcAllocContourSet());
    if (!contours || !rcBuildContours(&context, *compact, build.maxSimplificationError,
                                      static_cast<i32>(build.maxEdgeLength / cs), *contours))
    {
        fail("build the contours");
        return;
    }
    if (contours->nconts == 0)
    {
        return;
    }
    RecastPtr<rcPolyMesh> polygons(rcAllocPolyMesh());
    if (!polygons || !rcBuildPolyMesh(&context, *contours, DT_VERTS_PER_POLYGON, *polygons))
    {
        fail("build the polygons");
        return;
    }
    RecastPtr<rcPolyMeshDetail> detailMesh(rcAllocPolyMeshDetail());
    const f32 sampleDistance = build.detailSampleDistance < 0.9f ? 0.0f : cs * build.detailSampleDistance;
    if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polygons, *compact, sampleDistance,
                                              ch * build.detailSampleMaxError, *detailMesh))
    {
        fail("build the detail mesh");
        return;
    }
    if (polygons->nverts == 0 || polygons->npolys == 0)
    {
        return;
    }
    if (polygons->nverts >= 0xffff)
    {
        fail("fit the tile's vertices in 16 bits: lower build.tileSize");
        return;
    }
    // Detour's area of a polygon is ours; its flags are one bit per area, so that area masks are include
    // flags.
    for (i32 i = 0; i < polygons->npolys; ++i)
    {
        const u8 area = static_cast<u8>(polygons->areas[i] - 1);
        polygons->areas[i] = area;
        polygons->flags[i] = static_cast<u16>(1u << area);
    }

    std::vector<f32> linkVertices;
    std::vector<f32> linkRadii;
    std::vector<u16> linkFlags;
    std::vector<u8> linkAreas;
    std::vector<u8> linkDirections;
    std::vector<u32> linkIds;
    for (usize i = 0; i < geometry.links.size(); ++i)
    {
        const NavLinkData& link = geometry.links[i];
        if (!insideXz(link.start, box))
        {
            continue;
        }
        linkVertices.insert(linkVertices.end(),
                            {link.start.x, link.start.y, link.start.z, link.end.x, link.end.y, link.end.z});
        linkRadii.push_back(link.radius);
        linkFlags.push_back(static_cast<u16>(1u << link.area));
        linkAreas.push_back(link.area);
        linkDirections.push_back(link.bidirectional ? u8{DT_OFFMESH_CON_BIDIR} : u8{0});
        linkIds.push_back(static_cast<u32>(i));
    }

    dtNavMeshCreateParams params{};
    params.verts = polygons->verts;
    params.vertCount = polygons->nverts;
    params.polys = polygons->polys;
    params.polyAreas = polygons->areas;
    params.polyFlags = polygons->flags;
    params.polyCount = polygons->npolys;
    params.nvp = polygons->nvp;
    params.detailMeshes = detailMesh->meshes;
    params.detailVerts = detailMesh->verts;
    params.detailVertsCount = detailMesh->nverts;
    params.detailTris = detailMesh->tris;
    params.detailTriCount = detailMesh->ntris;
    params.offMeshConVerts = linkVertices.data();
    params.offMeshConRad = linkRadii.data();
    params.offMeshConFlags = linkFlags.data();
    params.offMeshConAreas = linkAreas.data();
    params.offMeshConDir = linkDirections.data();
    params.offMeshConUserID = linkIds.data();
    params.offMeshConCount = static_cast<i32>(linkIds.size());
    params.tileX = tile.x;
    params.tileY = tile.z;
    params.tileLayer = 0;
    std::memcpy(params.bmin, polygons->bmin, sizeof(params.bmin));
    std::memcpy(params.bmax, polygons->bmax, sizeof(params.bmax));
    params.walkableHeight = agent.height;
    params.walkableRadius = agent.radius;
    params.walkableClimb = agent.maxClimb;
    params.cs = cs;
    params.ch = ch;
    params.buildBvTree = true;

    unsigned char* data = nullptr;
    i32 dataSize = 0;
    if (!dtCreateNavMeshData(&params, &data, &dataSize))
    {
        fail("create the Detour tile");
        return;
    }
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    result.detour.assign(bytes, bytes + dataSize);
    dtFree(data);
}

// Candidate triangles of every tile, row by row: a cheap superset (one tile wider) in ascending order, which
// each tile filters exactly with selectTriangles().
[[nodiscard]] std::vector<std::vector<u32>> tileCandidates(const NavGeometry& geometry, const TileGrid& grid)
{
    std::vector<std::vector<u32>> candidates(usize{grid.tilesX} * grid.tilesZ);
    // Clamped before the conversion, so that far vertices of hand-made input cannot overflow it.
    const f64 limit = static_cast<f64>(std::max(grid.tilesX, grid.tilesZ)) + 2.0;
    const auto cell = [&](f32 value, f32 origin) {
        const f64 index = std::floor(static_cast<f64>(value - origin) / static_cast<f64>(grid.tileWorld));
        return static_cast<i64>(std::clamp(index, -2.0, limit));
    };
    const usize triangleCount = geometry.indices.size() / 3;
    for (usize triangle = 0; triangle < triangleCount; ++triangle)
    {
        const Aabb bounds = triangleBounds(geometry, triangle);
        const i64 x0 = std::max<i64>(cell(bounds.min.x - grid.border, grid.origin.x) - 1, 0);
        const i64 x1 =
            std::min<i64>(cell(bounds.max.x + grid.border, grid.origin.x) + 1, i64{grid.tilesX} - 1);
        const i64 z0 = std::max<i64>(cell(bounds.min.z - grid.border, grid.origin.z) - 1, 0);
        const i64 z1 =
            std::min<i64>(cell(bounds.max.z + grid.border, grid.origin.z) + 1, i64{grid.tilesZ} - 1);
        for (i64 z = z0; z <= z1; ++z)
        {
            for (i64 x = x0; x <= x1; ++x)
            {
                candidates[static_cast<usize>(z) * grid.tilesX + static_cast<usize>(x)].push_back(
                    static_cast<u32>(triangle));
            }
        }
    }
    return candidates;
}

[[nodiscard]] std::span<const u32> candidatesOf(const std::vector<std::vector<u32>>& candidates,
                                                const TileGrid& grid, NavTileCoord tile) noexcept
{
    return candidates[static_cast<usize>(tile.z) * grid.tilesX + static_cast<usize>(tile.x)];
}

// Builds the tiles at @p coords of @p geometry's grid (in parallel on @p jobs when given).
void buildTiles(const NavGeometry& geometry, const TileGrid& grid,
                const std::vector<std::vector<u32>>& candidates, std::span<const NavTileCoord> coords,
                JobSystem* jobs, std::vector<TileResult>& results)
{
    ILL_PROFILE_SCOPE();
    detail::installNavAllocators();
    results.assign(coords.size(), TileResult{});
    const auto build = [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i)
        {
            buildTile(geometry, grid, coords[i], candidatesOf(candidates, grid, coords[i]), results[i]);
        }
    };
    const u32 count = static_cast<u32>(coords.size());
    if (jobs != nullptr)
    {
        jobs->parallelFor(count, 1, build);
    }
    else
    {
        build(0, count);
    }
}

[[nodiscard]] Expected<void, std::string> firstError(const std::vector<TileResult>& results)
{
    for (const TileResult& result : results)
    {
        if (!result.error.empty())
        {
            return unexpected(result.error);
        }
    }
    return {};
}

void describeGrid(const NavGeometry& geometry, const TileGrid& grid, NavMeshData& data)
{
    data.agent = geometry.agent;
    data.build = geometry.build;
    data.origin = grid.origin;
    data.tileWorldSize = grid.tileWorld;
    data.tilesX = grid.tilesX;
    data.tilesZ = grid.tilesZ;
    data.boundsMin = geometry.bounds.min;
    data.boundsMax = geometry.bounds.max;
}

[[nodiscard]] bool sameGrid(const NavMeshData& a, const NavMeshData& b) noexcept
{
    const auto bits = [](const auto& value) {
        return std::as_bytes(std::span(&value, 1));
    };
    const auto same = [&](const auto& x, const auto& y) {
        return std::ranges::equal(bits(x), bits(y));
    };
    const NavBuildSettings& p = a.build;
    const NavBuildSettings& q = b.build;
    return a.agent.agentType == b.agent.agentType && same(a.agent.radius, b.agent.radius) &&
           same(a.agent.height, b.agent.height) && same(a.agent.maxClimb, b.agent.maxClimb) &&
           same(a.agent.maxSlope, b.agent.maxSlope) && same(p.cellSize, q.cellSize) &&
           same(p.cellHeight, q.cellHeight) && p.tileSize == q.tileSize &&
           same(p.minRegionArea, q.minRegionArea) && same(p.mergeRegionArea, q.mergeRegionArea) &&
           same(p.maxEdgeLength, q.maxEdgeLength) &&
           same(p.maxSimplificationError, q.maxSimplificationError) &&
           same(p.detailSampleDistance, q.detailSampleDistance) &&
           same(p.detailSampleMaxError, q.detailSampleMaxError) && same(a.origin.x, b.origin.x) &&
           same(a.origin.y, b.origin.y) && same(a.origin.z, b.origin.z) &&
           same(a.tileWorldSize, b.tileWorldSize) && a.tilesX == b.tilesX && a.tilesZ == b.tilesZ &&
           same(a.boundsMin.x, b.boundsMin.x) && same(a.boundsMin.y, b.boundsMin.y) &&
           same(a.boundsMin.z, b.boundsMin.z) && same(a.boundsMax.x, b.boundsMax.x) &&
           same(a.boundsMax.y, b.boundsMax.y) && same(a.boundsMax.z, b.boundsMax.z);
}

// The tiles of @p data cover its grid row by row (as bakeNavMesh() and decodeNavMeshData() make them).
[[nodiscard]] bool tilesInGrid(const NavMeshData& data) noexcept
{
    if (data.tiles.size() != usize{data.tilesX} * data.tilesZ)
    {
        return false;
    }
    for (usize i = 0; i < data.tiles.size(); ++i)
    {
        if (data.tiles[i].x < 0 || data.tiles[i].z < 0 ||
            u64(i) != u64(data.tiles[i].z) * data.tilesX + u64(data.tiles[i].x))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<EntityGuid> linkGuids(const NavGeometry& geometry)
{
    std::vector<EntityGuid> guids;
    guids.reserve(geometry.links.size());
    for (const NavLinkData& link : geometry.links)
    {
        guids.push_back(link.entity);
    }
    return guids;
}

[[nodiscard]] EntityGuid guidOf(const World& world, Entity entity) noexcept
{
    const EntityGuid* guid = world.tryGet<EntityGuid>(entity);
    return guid != nullptr ? *guid : EntityGuid{};
}

} // namespace

Expected<NavGeometry, std::string> collectNavGeometry(const World& world, Entity surface,
                                                      const NavMeshLookup& findMesh)
{
    ILL_PROFILE_SCOPE();
    if (!world.isAlive(surface))
    {
        return unexpected(std::string("the surface Entity does not exist"));
    }
    const NavMeshSurface* settings = world.tryGet<NavMeshSurface>(surface);
    if (settings == nullptr)
    {
        return unexpected(std::string("the Entity has no NavMeshSurface"));
    }
    NavGeometry geometry;
    geometry.agent = NavAgentProfile{.agentType = settings->agentType,
                                     .radius = settings->agentRadius,
                                     .height = settings->agentHeight,
                                     .maxClimb = settings->agentMaxClimb,
                                     .maxSlope = settings->agentMaxSlope};
    geometry.build = settings->build;
    if (std::string error = detail::navSettingsError(geometry.agent, geometry.build); !error.empty())
    {
        return unexpected(std::move(error));
    }
    const Vec3& size = settings->size;
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) || size.x <= 0.0f ||
        size.y <= 0.0f || size.z <= 0.0f)
    {
        return unexpected(std::string("size must be positive in every axis"));
    }
    for (const f32 cost : settings->areaCosts)
    {
        if (!detail::finiteAtLeast(cost, 1.0f))
        {
            return unexpected(std::string("areaCosts must be at least 1"));
        }
    }
    const Transform* surfaceTransform = world.tryGet<Transform>(surface);
    const Vec3 center = surfaceTransform != nullptr ? surfaceTransform->position : Vec3(0.0f);
    geometry.bounds = Aabb{.min = center - size * 0.5f, .max = center + size * 0.5f};
    Aabb filter = geometry.bounds;
    const f32 border =
        static_cast<f32>(borderCells(geometry.agent, geometry.build)) * geometry.build.cellSize;
    filter.min -= Vec3(border, 0.0f, border);
    filter.max += Vec3(border, 0.0f, border);

    std::vector<Entity> entities;
    world.entities(entities);
    TriangleSink sink(geometry, filter);
    const u32 agentBit = 1u << geometry.agent.agentType;
    for (const Entity entity : entities)
    {
        if (world.has<Inactive>(entity))
        {
            continue;
        }
        const Transform* transform = world.tryGet<Transform>(entity);
        const Mat4 matrix = transform != nullptr ? transform->matrix() : Mat4(1.0f);
        const NavArea* navArea = world.tryGet<NavArea>(entity);
        if (navArea != nullptr && navArea->area >= kMaxNavAreas)
        {
            return unexpected(std::string("a NavArea's area is out of range"));
        }

        if (const Collider* collider = world.tryGet<Collider>(entity))
        {
            const RigidBody* body = world.tryGet<RigidBody>(entity);
            const u32 layer = body != nullptr ? body->layer : 0u;
            const bool moves = body != nullptr && body->motionType == MotionType::Dynamic;
            if (!moves && layer < 32 && ((settings->layerMask >> layer) & 1u) != 0)
            {
                u8 area = 0;
                if (navArea != nullptr && !navArea->volume)
                {
                    area = navArea->walkable ? navArea->area : kNavAreaNotWalkable;
                }
                const Mat4 shapeMatrix = matrix * glm::translate(Mat4(1.0f), collider->offset) *
                                         glm::mat4_cast(glm::normalize(collider->rotation));
                sink.begin(shapeMatrix, area);
                switch (collider->shape)
                {
                case ColliderShape::Box:
                    addBox(sink, collider->halfExtents);
                    break;
                case ColliderShape::Sphere:
                    addRounded(sink, collider->radius, 0.0f);
                    break;
                case ColliderShape::Capsule:
                    addRounded(sink, collider->radius, collider->halfHeight);
                    break;
                case ColliderShape::Cylinder:
                    addCylinder(sink, collider->radius, collider->halfHeight);
                    break;
                case ColliderShape::ConvexHull:
                case ColliderShape::Mesh:
                    if (!collider->mesh)
                    {
                        ++geometry.skippedColliders;
                    }
                    else if (const MeshData* mesh =
                                 findMesh ? findMesh(collider->mesh, collider->meshIndex) : nullptr)
                    {
                        addMeshData(sink, *mesh);
                    }
                    else
                    {
                        ++geometry.missingMeshes;
                    }
                    break;
                }
            }
        }

        const Vec3 position = transform != nullptr ? transform->position : Vec3(0.0f);
        if (navArea != nullptr && navArea->volume)
        {
            const NavVolumeData volume{.center = position,
                                       .halfExtents = navArea->size * 0.5f,
                                       .yaw = 0.0f,
                                       .cylinder = false,
                                       .area = navArea->walkable ? navArea->area : kNavAreaNotWalkable};
            if (overlaps(volumeBounds(volume), filter))
            {
                geometry.volumes.push_back(volume);
            }
        }
        if (const NavObstacle* obstacle = world.tryGet<NavObstacle>(entity);
            obstacle != nullptr && obstacle->carve)
        {
            const bool cylinder = obstacle->shape == NavObstacleShape::Cylinder;
            const NavVolumeData volume{
                .center = position,
                .halfExtents = cylinder ? Vec3(obstacle->radius, obstacle->height * 0.5f, obstacle->radius)
                                        : obstacle->size * 0.5f,
                .yaw = cylinder || transform == nullptr ? 0.0f : yawOf(transform->rotation),
                .cylinder = cylinder,
                .area = kNavAreaNotWalkable};
            if (overlaps(volumeBounds(volume), filter))
            {
                geometry.volumes.push_back(volume);
            }
        }
        if (const NavLink* link = world.tryGet<NavLink>(entity);
            link != nullptr && (link->agentTypeMask & agentBit) != 0)
        {
            if (link->area >= kMaxNavAreas)
            {
                return unexpected(std::string("a NavLink's area is out of range"));
            }
            const NavLinkData data{.start = position,
                                   .end = Vec3(matrix * Vec4(link->end, 1.0f)),
                                   .radius = link->radius,
                                   .area = link->area,
                                   .kind = link->kind,
                                   .bidirectional = link->bidirectional,
                                   .entity = guidOf(world, entity)};
            if (insideXz(data.start, filter) || insideXz(data.end, filter))
            {
                geometry.links.push_back(data);
            }
        }
    }
    return geometry;
}

u64 navTileInputHash(const NavGeometry& geometry, NavTileCoord tile)
{
    const Expected<TileGrid, std::string> grid = makeGrid(geometry);
    if (!grid)
    {
        return 0;
    }
    std::vector<u32> all(geometry.indices.size() / 3);
    for (usize i = 0; i < all.size(); ++i)
    {
        all[i] = static_cast<u32>(i);
    }
    std::vector<u32> triangles;
    selectTriangles(geometry, tileBox(geometry, *grid, tile), all, triangles);
    return hashTileInput(geometry, *grid, tile, triangles);
}

Expected<NavMeshData, std::string> bakeNavMesh(const NavGeometry& geometry, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    Expected<TileGrid, std::string> grid = makeGrid(geometry);
    if (!grid)
    {
        return unexpected(std::move(grid.error()));
    }
    std::vector<NavTileCoord> coords;
    coords.reserve(usize{grid->tilesX} * grid->tilesZ);
    for (u32 z = 0; z < grid->tilesZ; ++z)
    {
        for (u32 x = 0; x < grid->tilesX; ++x)
        {
            coords.push_back(NavTileCoord{.x = static_cast<i32>(x), .z = static_cast<i32>(z)});
        }
    }
    std::vector<TileResult> results;
    buildTiles(geometry, *grid, tileCandidates(geometry, *grid), coords, jobs, results);
    if (Expected<void, std::string> failed = firstError(results); !failed)
    {
        return unexpected(std::move(failed.error()));
    }
    NavMeshData data;
    describeGrid(geometry, *grid, data);
    data.links = linkGuids(geometry);
    data.tiles.resize(coords.size());
    for (usize i = 0; i < coords.size(); ++i)
    {
        data.tiles[i] = NavTileData{.x = coords[i].x,
                                    .z = coords[i].z,
                                    .inputHash = results[i].inputHash,
                                    .detour = std::move(results[i].detour)};
    }
    return data;
}

Expected<void, std::string> rebakeStaleTiles(const NavGeometry& geometry, JobSystem* jobs, NavMeshData& data,
                                             std::vector<NavTileCoord>& rebuilt)
{
    ILL_PROFILE_SCOPE();
    Expected<TileGrid, std::string> grid = makeGrid(geometry);
    if (!grid)
    {
        return unexpected(std::move(grid.error()));
    }
    NavMeshData expected;
    describeGrid(geometry, *grid, expected);
    if (!sameGrid(expected, data) || !tilesInGrid(data))
    {
        Expected<NavMeshData, std::string> baked = bakeNavMesh(geometry, jobs);
        if (!baked)
        {
            return unexpected(std::move(baked.error()));
        }
        data = std::move(*baked);
        rebuilt.clear();
        for (const NavTileData& tile : data.tiles)
        {
            rebuilt.push_back(NavTileCoord{.x = tile.x, .z = tile.z});
        }
        return {};
    }

    const std::vector<std::vector<u32>> candidates = tileCandidates(geometry, *grid);
    std::vector<NavTileCoord> stale;
    std::vector<usize> staleIndices;
    std::vector<u32> triangles;
    for (usize i = 0; i < data.tiles.size(); ++i)
    {
        const NavTileCoord coord{.x = data.tiles[i].x, .z = data.tiles[i].z};
        triangles.clear();
        selectTriangles(geometry, tileBox(geometry, *grid, coord), candidatesOf(candidates, *grid, coord),
                        triangles);
        if (hashTileInput(geometry, *grid, coord, triangles) != data.tiles[i].inputHash)
        {
            stale.push_back(coord);
            staleIndices.push_back(i);
        }
    }
    std::vector<TileResult> results;
    buildTiles(geometry, *grid, candidates, stale, jobs, results);
    if (Expected<void, std::string> failed = firstError(results); !failed)
    {
        return unexpected(std::move(failed.error()));
    }
    for (usize i = 0; i < stale.size(); ++i)
    {
        NavTileData& tile = data.tiles[staleIndices[i]];
        tile.inputHash = results[i].inputHash;
        tile.detour = std::move(results[i].detour);
    }
    data.links = linkGuids(geometry);
    rebuilt = std::move(stale);
    return {};
}

namespace detail
{

Expected<NavTileGrid, std::string> makeNavTileGrid(const NavGeometry& geometry)
{
    return makeGrid(geometry);
}

std::vector<std::vector<u32>> navTileCandidates(const NavGeometry& geometry, const NavTileGrid& grid)
{
    return tileCandidates(geometry, grid);
}

u64 navTileHash(const NavGeometry& geometry, const NavTileGrid& grid, NavTileCoord tile,
                std::span<const u32> candidates)
{
    std::vector<u32> triangles;
    selectTriangles(geometry, tileBox(geometry, grid, tile), candidates, triangles);
    return hashTileInput(geometry, grid, tile, triangles);
}

void buildNavTile(const NavGeometry& geometry, const NavTileGrid& grid, NavTileCoord tile,
                  std::span<const u32> candidates, NavTileResult& result)
{
    installNavAllocators();
    buildTile(geometry, grid, tile, candidates, result);
}

Aabb navVolumeBounds(const NavVolumeData& volume) noexcept
{
    return volumeBounds(volume);
}

f32 navYaw(const Quat& rotation) noexcept
{
    return yawOf(rotation);
}

} // namespace detail

} // namespace illusion
