#pragma once

#include <illusion/core/Base.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavMeshData.hpp>

#include <cmath>
#include <string>

namespace illusion::detail
{

/// Most tiles of one navmesh grid (Detour's 64-bit references have room for more).
inline constexpr u64 kMaxNavTiles = u64{1} << 20;

/// Recast needs the agent's headroom to span at least this many cells.
inline constexpr f32 kMinWalkableHeightCells = 3.0f;

/// Recast's largest span height in cells (RC_SPAN_MAX_HEIGHT).
inline constexpr f32 kMaxHeightCells = 8191.0f;

[[nodiscard]] inline bool finiteAtLeast(f32 value, f32 minimum) noexcept
{
    return std::isfinite(value) && value >= minimum;
}

/// Why @p agent and @p build cannot be baked, or an empty string.
[[nodiscard]] inline std::string navSettingsError(const NavAgentProfile& agent, const NavBuildSettings& build)
{
    if (agent.agentType >= kMaxNavAgentTypes)
    {
        return "agentType must be below 8";
    }
    if (!finiteAtLeast(build.cellSize, 0.01f))
    {
        return "build.cellSize must be at least 0.01 m";
    }
    if (!finiteAtLeast(build.cellHeight, 0.01f))
    {
        return "build.cellHeight must be at least 0.01 m";
    }
    if (build.tileSize < 16 || build.tileSize > 1024)
    {
        return "build.tileSize must be in [16, 1024]";
    }
    if (!finiteAtLeast(build.minRegionArea, 0.0f) || !finiteAtLeast(build.mergeRegionArea, 0.0f))
    {
        return "build.minRegionArea and build.mergeRegionArea must not be negative";
    }
    if (!finiteAtLeast(build.maxEdgeLength, 0.0f) || !finiteAtLeast(build.maxSimplificationError, 0.0f))
    {
        return "build.maxEdgeLength and build.maxSimplificationError must not be negative";
    }
    if (!finiteAtLeast(build.detailSampleDistance, 0.0f) || !finiteAtLeast(build.detailSampleMaxError, 0.0f))
    {
        return "build.detailSampleDistance and build.detailSampleMaxError must not be negative";
    }
    if (!finiteAtLeast(agent.radius, 0.0f) || !finiteAtLeast(agent.maxClimb, 0.0f))
    {
        return "agentRadius and agentMaxClimb must not be negative";
    }
    if (!std::isfinite(agent.height) || agent.height < kMinWalkableHeightCells * build.cellHeight)
    {
        return "agentHeight must be at least 3 cells (3 x build.cellHeight)";
    }
    if (!finiteAtLeast(agent.maxSlope, 0.0f) || agent.maxSlope > 1.5707964f)
    {
        return "agentMaxSlope must be in [0, 90] degrees";
    }
    return {};
}

} // namespace illusion::detail
