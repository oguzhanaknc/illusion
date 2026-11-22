#pragma once

// The tile-level steps of the bake (NavBuild.cpp) that NavWorld runs one tile at a time in the background.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/nav/NavBuild.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace illusion::detail
{

/// The tile grid of a surface.
struct NavTileGrid
{
    Vec3 origin{0.0f};
    f32 tileWorld = 0.0f;
    f32 border = 0.0f; // in m
    i32 borderCells = 0;
    u32 tilesX = 0;
    u32 tilesZ = 0;
};

/// One built tile.
struct NavTileResult
{
    std::vector<std::byte> detour;
    u64 inputHash = 0;
    std::string error;
};

/// The grid of @p geometry, or why it cannot be baked.
[[nodiscard]] Expected<NavTileGrid, std::string> makeNavTileGrid(const NavGeometry& geometry);

/// The candidate triangles of every tile of @p grid, row by row (a superset, ascending).
[[nodiscard]] std::vector<std::vector<u32>> navTileCandidates(const NavGeometry& geometry,
                                                              const NavTileGrid& grid);

/// The input hash of @p tile, from its @p candidates (navTileCandidates()).
[[nodiscard]] u64 navTileHash(const NavGeometry& geometry, const NavTileGrid& grid, NavTileCoord tile,
                              std::span<const u32> candidates);

/// Builds @p tile from its @p candidates; thread-safe for distinct @p result.
void buildNavTile(const NavGeometry& geometry, const NavTileGrid& grid, NavTileCoord tile,
                  std::span<const u32> candidates, NavTileResult& result);

/// The world-space box that @p volume covers.
[[nodiscard]] Aabb navVolumeBounds(const NavVolumeData& volume) noexcept;

/// The rotation about +y of @p rotation, as collectNavGeometry() reads it for obstacles.
[[nodiscard]] f32 navYaw(const Quat& rotation) noexcept;

} // namespace illusion::detail
