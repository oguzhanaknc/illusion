#pragma once

/// @file
/// Baking navmeshes (docs/adr/0062-navigation-and-ai.md): the input of one NavMeshSurface gathered from a
/// World as plain data, and the pure functions that build its tiles with Recast. The editor's `nav.bake`, a
/// load without baked data and the run-time rebuild of changed tiles all go through these.
///
/// @code
/// auto geometry = illusion::collectNavGeometry(world, surface, findMesh);
/// if (geometry)
/// {
///     auto data = illusion::bakeNavMesh(*geometry, &jobs);
///     if (data)
///     {
///         writeFile("assets:/navigation/forest.navmesh", illusion::encodeNavMeshData(*data));
///     }
/// }
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavMeshData.hpp>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace illusion
{

class JobSystem;
class World;
struct MeshData;

/// Area of bake input that is cut out of the navmesh (NavArea::walkable false, a carving NavObstacle).
inline constexpr u8 kNavAreaNotWalkable = 0xFFu;

/// A box or cylinder of bake input that changes the area of what it covers.
struct NavVolumeData
{
    Vec3 center{0.0f};      ///< Centre in world space.
    Vec3 halfExtents{0.5f}; ///< Box: half size; cylinder: radius in x and z, half height in y.
    f32 yaw = 0.0f;         ///< Box: rotation about +y in radians.
    bool cylinder = false;  ///< Upright cylinder instead of a box.
    u8 area = 0;            ///< Area it marks, or kNavAreaNotWalkable.
};

/// An off-mesh link of bake input.
struct NavLinkData
{
    Vec3 start{0.0f};                     ///< Start in world space.
    Vec3 end{0.0f};                       ///< End in world space.
    f32 radius = 0.5f;                    ///< Snap radius of both ends in m.
    u8 area = 0;                          ///< Area type.
    NavLinkKind kind = NavLinkKind::Jump; ///< How it is crossed.
    bool bidirectional = true;            ///< Also from end to start.
    EntityGuid entity;                    ///< The NavLink's Entity.
};

/// Everything the bake of one surface reads, in world space.
struct NavGeometry
{
    NavAgentProfile agent{};            ///< The surface's agent profile.
    NavBuildSettings build{};           ///< The surface's build settings.
    Aabb bounds;                        ///< The surface's box.
    std::vector<Vec3> vertices;         ///< Triangle vertices.
    std::vector<u32> indices;           ///< Three per triangle.
    std::vector<u8> areas;              ///< One per triangle: an area type or kNavAreaNotWalkable.
    std::vector<NavVolumeData> volumes; ///< NavArea volumes and carving obstacles, in Entity order.
    std::vector<NavLinkData> links; ///< Links whose agentTypeMask has the surface's type, in Entity order.
    u32 missingMeshes = 0;          ///< Mesh colliders skipped because findMesh had no mesh for them.
    u32 skippedColliders = 0;       ///< Colliders with code-made geometry only, skipped.
};

/// Finds the mesh of a ConvexHull or Mesh Collider (Collider::mesh and Collider::meshIndex); null when it has
/// not loaded.
using NavMeshLookup = std::function<const MeshData*(const AssetRef& mesh, u32 meshIndex)>;

/// Gathers the bake input of @p surface's NavMeshSurface from @p world: the triangles of the Colliders inside
/// its box on its layers whose body is Static or Kinematic (or that have no RigidBody), with NavArea areas,
/// NavArea volumes, NavLinks and carving NavObstacles; Inactive Entities are skipped. Fails if @p surface has
/// no NavMeshSurface or its values are out of range. Allocates.
[[nodiscard]] Expected<NavGeometry, std::string> collectNavGeometry(const World& world, Entity surface,
                                                                    const NavMeshLookup& findMesh);

/// A tile of a surface's grid.
struct NavTileCoord
{
    i32 x = 0; ///< Column.
    i32 z = 0; ///< Row.

    /// Coordinates are equal when both match.
    friend constexpr bool operator==(const NavTileCoord&, const NavTileCoord&) noexcept = default;
};

/// Hash of the part of @p geometry that the tile at @p tile reads (its box and border), and of the settings.
[[nodiscard]] u64 navTileInputHash(const NavGeometry& geometry, NavTileCoord tile);

/// Builds every tile of @p geometry, as parallel Jobs on @p jobs when given; the bytes are the same for any
/// number of workers. Fails naming the setting out of range or a tile that Recast could not build.
[[nodiscard]] Expected<NavMeshData, std::string> bakeNavMesh(const NavGeometry& geometry, JobSystem* jobs);

/// Replaces @p data's tiles whose inputHash differs from @p geometry (all of them when the grid or settings
/// differ) by building them again, and writes their coordinates into @p rebuilt. Fails like bakeNavMesh(),
/// leaving @p data unchanged.
Expected<void, std::string> rebakeStaleTiles(const NavGeometry& geometry, JobSystem* jobs, NavMeshData& data,
                                             std::vector<NavTileCoord>& rebuilt);

} // namespace illusion
