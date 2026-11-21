// The .navmesh container (ADR 0062): a round trip of every field and every NavDataError.

#include <illusion/core/Hash.hpp>
#include <illusion/nav/NavMeshData.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

using namespace illusion;

namespace
{

NavMeshData sample()
{
    NavMeshData data;
    data.agent =
        NavAgentProfile{.agentType = 3, .radius = 0.4f, .height = 2.0f, .maxClimb = 0.5f, .maxSlope = 0.7f};
    data.build.cellSize = 0.2f;
    data.build.tileSize = 32;
    data.build.detailSampleDistance = 0.0f;
    data.origin = Vec3(-8.0f, -1.0f, -4.0f);
    data.tileWorldSize = 6.4f;
    data.tilesX = 2;
    data.tilesZ = 1;
    data.boundsMin = Vec3(-8.0f, -1.0f, -4.0f);
    data.boundsMax = Vec3(4.0f, 3.0f, 2.0f);
    data.tiles = {NavTileData{.x = 0, .z = 0, .inputHash = 11, .detour = {std::byte{1}, std::byte{2}}},
                  NavTileData{.x = 1, .z = 0, .inputHash = 12, .detour = {}}};
    data.links = {EntityGuid{.value = 42}, EntityGuid{.value = 43}};
    return data;
}

// Writes @p value at @p offset and recomputes the checksum.
void patch(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
    const u64 checksum = xxhash64(std::span(bytes).first(bytes.size() - sizeof(u64)));
    std::memcpy(bytes.data() + bytes.size() - sizeof(u64), &checksum, sizeof(checksum));
}

NavDataError errorOf(std::span<const std::byte> bytes)
{
    const Expected<NavMeshData, NavDataError> decoded = decodeNavMeshData(bytes);
    REQUIRE_FALSE(decoded.hasValue());
    return decoded.error();
}

} // namespace

TEST_CASE("a navmesh round-trips through its file")
{
    const NavMeshData data = sample();
    const std::vector<std::byte> bytes = encodeNavMeshData(data);
    const Expected<NavMeshData, NavDataError> decoded = decodeNavMeshData(bytes);
    REQUIRE(decoded.hasValue());
    CHECK(decoded->agent.agentType == 3);
    CHECK(decoded->agent.radius == 0.4f);
    CHECK(decoded->agent.height == 2.0f);
    CHECK(decoded->agent.maxClimb == 0.5f);
    CHECK(decoded->agent.maxSlope == 0.7f);
    CHECK(decoded->build.cellSize == 0.2f);
    CHECK(decoded->build.tileSize == 32);
    CHECK(decoded->build.detailSampleDistance == 0.0f);
    CHECK(decoded->build.maxEdgeLength == data.build.maxEdgeLength);
    CHECK(decoded->origin == data.origin);
    CHECK(decoded->tileWorldSize == 6.4f);
    CHECK(decoded->tilesX == 2);
    CHECK(decoded->tilesZ == 1);
    CHECK(decoded->boundsMin == data.boundsMin);
    CHECK(decoded->boundsMax == data.boundsMax);
    REQUIRE(decoded->tiles.size() == 2);
    CHECK(decoded->tiles[0].inputHash == 11);
    CHECK(decoded->tiles[0].detour == data.tiles[0].detour);
    CHECK(decoded->tiles[1].x == 1);
    CHECK(decoded->tiles[1].detour.empty());
    CHECK(decoded->links == data.links);
    CHECK(encodeNavMeshData(*decoded) == bytes);
    CHECK(std::memcmp(bytes.data(), "ILNAVMSH", 8) == 0);
}

TEST_CASE("decodeNavMeshData rejects other files, versions, truncation, checksums and invalid grids")
{
    const std::vector<std::byte> bytes = encodeNavMeshData(sample());

    CHECK(errorOf({}) == NavDataError::NotNavMesh);
    std::vector<std::byte> other = bytes;
    other[0] = std::byte{'X'};
    CHECK(errorOf(other) == NavDataError::NotNavMesh);

    std::vector<std::byte> version = bytes;
    patch(version, 8, kNavMeshFormatVersion + 1);
    CHECK(errorOf(version) == NavDataError::Version);

    std::vector<std::byte> detour = bytes;
    patch(detour, 12, 0);
    CHECK(errorOf(detour) == NavDataError::DetourVersion);

    for (const usize size : {usize{10}, usize{40}, bytes.size() / 2, bytes.size() - 1})
    {
        CAPTURE(size);
        CHECK(errorOf(std::span(bytes).first(size)) == NavDataError::Truncated);
    }

    std::vector<std::byte> corrupt = bytes;
    corrupt[20] ^= std::byte{0x01}; // the agent radius
    CHECK(errorOf(corrupt) == NavDataError::Checksum);

    std::vector<std::byte> longer = bytes;
    longer.push_back(std::byte{0});
    CHECK(errorOf(longer) == NavDataError::Invalid);

    NavMeshData duplicate = sample();
    duplicate.tiles[1].x = 0;
    CHECK(errorOf(encodeNavMeshData(duplicate)) == NavDataError::Invalid);

    NavMeshData missing = sample();
    missing.tiles.pop_back();
    CHECK(errorOf(encodeNavMeshData(missing)) == NavDataError::Invalid);

    NavMeshData outside = sample();
    outside.tiles[1].z = 1;
    CHECK(errorOf(encodeNavMeshData(outside)) == NavDataError::Invalid);

    NavMeshData settings = sample();
    settings.build.tileSize = 0;
    CHECK(errorOf(encodeNavMeshData(settings)) == NavDataError::Invalid);
}

TEST_CASE("NavDataError values have stable names")
{
    CHECK(navDataErrorName(NavDataError::NotNavMesh) == "NotNavMesh");
    CHECK(navDataErrorName(NavDataError::Version) == "Version");
    CHECK(navDataErrorName(NavDataError::DetourVersion) == "DetourVersion");
    CHECK(navDataErrorName(NavDataError::Truncated) == "Truncated");
    CHECK(navDataErrorName(NavDataError::Checksum) == "Checksum");
    CHECK(navDataErrorName(NavDataError::Invalid) == "Invalid");
}
