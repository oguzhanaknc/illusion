#pragma once

/// @file
/// The baked navmesh of one NavMeshSurface and its `.navmesh` file (docs/adr/0062-navigation-and-ai.md):
/// the agent profile and build settings it was baked with, and one Detour tile per grid cell. Plain data
/// and the container's encoder and decoder; the tile bytes are opaque outside engine/nav.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/nav/NavComponents.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

/// File extension of baked navmeshes.
inline constexpr std::string_view kNavMeshExtension = ".navmesh";

/// Version of the `.navmesh` container that encodeNavMeshData() writes and decodeNavMeshData() reads.
inline constexpr u32 kNavMeshFormatVersion = 1;

/// The agent a navmesh was baked for (the agent fields of NavMeshSurface).
struct NavAgentProfile
{
    u32 agentType = 0;          ///< In [0, kMaxNavAgentTypes).
    f32 radius = 0.3f;          ///< Clearance from walls in m.
    f32 height = 1.8f;          ///< Headroom in m.
    f32 maxClimb = 0.35f;       ///< Highest step in m.
    f32 maxSlope = 0.87266463f; ///< Steepest walkable slope in radians.
};

/// One baked tile.
struct NavTileData
{
    i32 x = 0;         ///< Column in the tile grid.
    i32 z = 0;         ///< Row in the tile grid.
    u64 inputHash = 0; ///< Hash of the bake input that overlaps the tile and its border, and the settings.
    /// Detour tile data (little-endian); empty for a tile without walkable polygons.
    std::vector<std::byte> detour;
};

/// The baked navmesh of one surface.
struct NavMeshData
{
    NavAgentProfile agent{};        ///< Agent profile.
    NavBuildSettings build{};       ///< Settings of the bake.
    Vec3 origin{0.0f};              ///< Minimum corner of the tile grid in world space.
    f32 tileWorldSize = 9.6f;       ///< Width of a tile in m (tileSize times cellSize).
    u32 tilesX = 0;                 ///< Columns of the grid.
    u32 tilesZ = 0;                 ///< Rows of the grid.
    Vec3 boundsMin{0.0f};           ///< Minimum corner of the surface's box.
    Vec3 boundsMax{0.0f};           ///< Maximum corner of the surface's box.
    std::vector<NavTileData> tiles; ///< Every tile of the grid, row by row.
    /// The NavLink Entities baked into the tiles; a link's Detour user id is its index here.
    std::vector<EntityGuid> links;
};

/// Why decodeNavMeshData() rejected a file.
enum class NavDataError : u8
{
    NotNavMesh,    ///< The magic is missing.
    Version,       ///< Another container version.
    DetourVersion, ///< Baked with another Detour tile version: bake it again.
    Truncated,     ///< The file ends early or a size field is out of range.
    Checksum,      ///< The checksum does not match.
    Invalid,       ///< A value is out of range (tile coordinates, settings, a duplicate tile).
};

/// Stable name of @p error, e.g. "Checksum".
[[nodiscard]] constexpr std::string_view navDataErrorName(NavDataError error) noexcept
{
    switch (error)
    {
    case NavDataError::NotNavMesh:
        return "NotNavMesh";
    case NavDataError::Version:
        return "Version";
    case NavDataError::DetourVersion:
        return "DetourVersion";
    case NavDataError::Truncated:
        return "Truncated";
    case NavDataError::Checksum:
        return "Checksum";
    case NavDataError::Invalid:
        return "Invalid";
    }
    return "Invalid";
}

/// Writes @p data as a `.navmesh` file: magic, versions, settings, tiles and an xxhash64 checksum.
[[nodiscard]] std::vector<std::byte> encodeNavMeshData(const NavMeshData& data);

/// Reads a `.navmesh` file written by encodeNavMeshData(); the tiles' Detour data is checked for size only.
[[nodiscard]] Expected<NavMeshData, NavDataError> decodeNavMeshData(std::span<const std::byte> bytes);

} // namespace illusion
