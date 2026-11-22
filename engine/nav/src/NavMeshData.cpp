#include "NavValidation.hpp"

#include <illusion/core/Hash.hpp>
#include <illusion/core/Serialization.hpp>
#include <illusion/nav/NavMeshData.hpp>

#include <DetourNavMesh.h>
#include <array>
#include <cmath>
#include <cstring>

namespace illusion
{
namespace
{

constexpr std::array<char, 8> kMagic{'I', 'L', 'N', 'A', 'V', 'M', 'S', 'H'};
constexpr usize kChecksumSize = sizeof(u64);
// Smallest record of one tile: coordinates, hash and the size of its Detour data.
constexpr usize kMinTileRecord = 2 * sizeof(i32) + sizeof(u64) + sizeof(u32);

void writeVec3(BinaryWriter& writer, const Vec3& value)
{
    writer.write(value.x);
    writer.write(value.y);
    writer.write(value.z);
}

bool readVec3(BinaryReader& reader, Vec3& value)
{
    return reader.read(value.x) && reader.read(value.y) && reader.read(value.z);
}

bool finiteVec3(const Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void writeSettings(BinaryWriter& writer, const NavAgentProfile& agent, const NavBuildSettings& build)
{
    writer.write(agent.agentType);
    writer.write(agent.radius);
    writer.write(agent.height);
    writer.write(agent.maxClimb);
    writer.write(agent.maxSlope);
    writer.write(build.cellSize);
    writer.write(build.cellHeight);
    writer.write(build.tileSize);
    writer.write(build.minRegionArea);
    writer.write(build.mergeRegionArea);
    writer.write(build.maxEdgeLength);
    writer.write(build.maxSimplificationError);
    writer.write(build.detailSampleDistance);
    writer.write(build.detailSampleMaxError);
}

bool readSettings(BinaryReader& reader, NavAgentProfile& agent, NavBuildSettings& build)
{
    return reader.read(agent.agentType) && reader.read(agent.radius) && reader.read(agent.height) &&
           reader.read(agent.maxClimb) && reader.read(agent.maxSlope) && reader.read(build.cellSize) &&
           reader.read(build.cellHeight) && reader.read(build.tileSize) && reader.read(build.minRegionArea) &&
           reader.read(build.mergeRegionArea) && reader.read(build.maxEdgeLength) &&
           reader.read(build.maxSimplificationError) && reader.read(build.detailSampleDistance) &&
           reader.read(build.detailSampleMaxError);
}

bool validGrid(const NavMeshData& data)
{
    if (!detail::navSettingsError(data.agent, data.build).empty())
    {
        return false;
    }
    if (!finiteVec3(data.origin) || !finiteVec3(data.boundsMin) || !finiteVec3(data.boundsMax) ||
        !std::isfinite(data.tileWorldSize) || data.tileWorldSize <= 0.0f)
    {
        return false;
    }
    if (data.boundsMin.x > data.boundsMax.x || data.boundsMin.y > data.boundsMax.y ||
        data.boundsMin.z > data.boundsMax.z)
    {
        return false;
    }
    const u64 tileCount = u64{data.tilesX} * u64{data.tilesZ};
    if (tileCount == 0 || tileCount > detail::kMaxNavTiles || data.tiles.size() != tileCount)
    {
        return false;
    }
    // Tiles are stored row by row, so a tile out of place is out of range or a duplicate.
    for (usize i = 0; i < data.tiles.size(); ++i)
    {
        const NavTileData& tile = data.tiles[i];
        if (u64(i) != u64(tile.z) * data.tilesX + u64(tile.x) || tile.x < 0 || tile.z < 0 ||
            u32(tile.x) >= data.tilesX || u32(tile.z) >= data.tilesZ)
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::byte> encodeNavMeshData(const NavMeshData& data)
{
    std::vector<std::byte> bytes;
    BinaryWriter writer(bytes);
    writer.writeBytes(std::as_bytes(std::span(kMagic)));
    writer.write(kNavMeshFormatVersion);
    writer.write(static_cast<u32>(DT_NAVMESH_VERSION));
    writeSettings(writer, data.agent, data.build);
    writeVec3(writer, data.origin);
    writer.write(data.tileWorldSize);
    writer.write(data.tilesX);
    writer.write(data.tilesZ);
    writeVec3(writer, data.boundsMin);
    writeVec3(writer, data.boundsMax);
    writer.write(static_cast<u32>(data.links.size()));
    for (const EntityGuid& link : data.links)
    {
        writer.write(link.value);
    }
    writer.write(static_cast<u32>(data.tiles.size()));
    for (const NavTileData& tile : data.tiles)
    {
        writer.write(tile.x);
        writer.write(tile.z);
        writer.write(tile.inputHash);
        writer.write(static_cast<u32>(tile.detour.size()));
        writer.writeBytes(tile.detour);
    }
    writer.write(xxhash64(bytes));
    return bytes;
}

Expected<NavMeshData, NavDataError> decodeNavMeshData(std::span<const std::byte> bytes)
{
    if (bytes.size() < kMagic.size() || std::memcmp(bytes.data(), kMagic.data(), kMagic.size()) != 0)
    {
        return unexpected(NavDataError::NotNavMesh);
    }
    BinaryReader reader(bytes.subspan(kMagic.size()));
    u32 version = 0;
    u32 detourVersion = 0;
    if (!reader.read(version) || !reader.read(detourVersion))
    {
        return unexpected(NavDataError::Truncated);
    }
    if (version != kNavMeshFormatVersion)
    {
        return unexpected(NavDataError::Version);
    }
    if (detourVersion != static_cast<u32>(DT_NAVMESH_VERSION))
    {
        return unexpected(NavDataError::DetourVersion);
    }

    NavMeshData data;
    u32 linkCount = 0;
    readSettings(reader, data.agent, data.build);
    readVec3(reader, data.origin);
    reader.read(data.tileWorldSize);
    reader.read(data.tilesX);
    reader.read(data.tilesZ);
    readVec3(reader, data.boundsMin);
    readVec3(reader, data.boundsMax);
    reader.read(linkCount);
    if (!reader.ok() || linkCount > reader.remaining() / sizeof(u64))
    {
        return unexpected(NavDataError::Truncated);
    }
    data.links.resize(linkCount);
    for (EntityGuid& link : data.links)
    {
        reader.read(link.value);
    }
    u32 tileCount = 0;
    reader.read(tileCount);
    if (!reader.ok() || tileCount > reader.remaining() / kMinTileRecord)
    {
        return unexpected(NavDataError::Truncated);
    }
    data.tiles.resize(tileCount);
    for (NavTileData& tile : data.tiles)
    {
        u32 size = 0;
        if (!reader.read(tile.x) || !reader.read(tile.z) || !reader.read(tile.inputHash) ||
            !reader.read(size) || size > reader.remaining())
        {
            return unexpected(NavDataError::Truncated);
        }
        const std::span<const std::byte> detour = reader.view(size);
        tile.detour.assign(detour.begin(), detour.end());
    }
    if (reader.remaining() < kChecksumSize)
    {
        return unexpected(NavDataError::Truncated);
    }
    if (reader.remaining() > kChecksumSize)
    {
        return unexpected(NavDataError::Invalid);
    }
    u64 checksum = 0;
    reader.read(checksum);
    if (checksum != xxhash64(bytes.first(bytes.size() - kChecksumSize)))
    {
        return unexpected(NavDataError::Checksum);
    }
    if (!validGrid(data))
    {
        return unexpected(NavDataError::Invalid);
    }
    return data;
}

} // namespace illusion
