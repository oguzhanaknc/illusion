// NavWorld surfaces and queries (ADR 0062): navmeshes baked at load, loaded from `.navmesh` files and live
// reloaded, stale and rebuilt tiles, carving obstacles, one surface per agent type, the four queries with
// area costs and masks, debug drawing, and queries that never allocate.

#include "TestSupport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavBuild.hpp>
#include <illusion/nav/NavMeshData.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

Entity addBox(World& world, Vec3 center, Vec3 halfExtents, Quat rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f))
{
    Transform transform;
    transform.position = center;
    transform.rotation = rotation;
    Collider collider;
    collider.shape = ColliderShape::Box;
    collider.halfExtents = halfExtents;
    RigidBody body;
    body.motionType = MotionType::Static;
    return world.create(transform, collider, body);
}

NavMeshSurface surfaceSettings(bool rebuild)
{
    NavMeshSurface surface;
    surface.size = Vec3(40.0f, 8.0f, 40.0f);
    surface.build.cellSize = 0.25f;
    surface.build.tileSize = 40;
    surface.rebuildAtRuntime = rebuild;
    return surface;
}

// A 40 m ground (4 x 4 tiles of 10 m) with a wall, a ramp up to a platform, a box reached by a jump link, a
// cost strip, a strip across the whole level and a small platform nothing connects.
struct Level
{
    World world;
    Entity surface;
    Entity wall;
    Entity link;

    explicit Level(bool rebuild = false)
    {
        Transform center;
        center.position = Vec3(0.0f, 2.0f, 0.0f);
        surface = world.create(center, surfaceSettings(rebuild));
        addBox(world, Vec3(0.0f, -0.5f, 0.0f), Vec3(20.0f, 0.5f, 20.0f));
        wall = addBox(world, Vec3(-5.0f, 1.0f, 3.0f), Vec3(6.0f, 1.0f, 0.25f));

        addBox(world, Vec3(12.0f, 1.0f, -12.0f), Vec3(3.0f, 1.0f, 3.0f));
        const f32 angle = std::atan2(2.0f, 6.0f);
        const Vec3 normal(-std::sin(angle), std::cos(angle), 0.0f);
        addBox(world, Vec3(6.0f, 1.0f, -12.0f) - normal * 0.1f, Vec3(std::sqrt(40.0f) * 0.5f, 0.1f, 1.5f),
               glm::angleAxis(angle, Vec3(0.0f, 0.0f, 1.0f)));

        addBox(world, Vec3(12.0f, 0.75f, 12.0f), Vec3(2.5f, 0.75f, 2.5f));
        Transform linkStart;
        linkStart.position = Vec3(8.0f, 0.0f, 12.0f);
        NavLink jump;
        jump.end = Vec3(3.0f, 1.5f, 0.0f);
        jump.area = 2;
        link = world.create(linkStart, jump, EntityGuid{.value = 0x1122});

        strip(Vec3(-10.0f, 0.0f, -5.0f), Vec3(20.0f, 2.0f, 2.0f), 3);
        strip(Vec3(0.0f, 0.0f, 17.0f), Vec3(40.0f, 2.0f, 2.0f), 5);

        addBox(world, Vec3(-15.0f, 1.0f, 8.0f), Vec3(1.1f, 1.0f, 1.1f));
    }

    void strip(Vec3 position, Vec3 size, u8 areaType)
    {
        Transform transform;
        transform.position = position;
        NavArea area;
        area.area = areaType;
        area.volume = true;
        area.size = size;
        world.create(transform, area);
    }

    void moveWall(Vec3 position)
    {
        world.get<Transform>(wall).position = position;
        world.markChanged<Transform>();
    }
};

struct Path
{
    std::vector<Vec3> corners;
    NavPathResult result;
};

Expected<Path, NavError> findPath(const NavWorld& navigation, Vec3 from, Vec3 to,
                                  const NavQueryFilter& filter = {})
{
    std::vector<Vec3> corners(256);
    Expected<NavPathResult, NavError> result = navigation.findPath(from, to, corners, filter);
    if (!result)
    {
        return unexpected(result.error());
    }
    corners.resize(result->count);
    return Path{.corners = std::move(corners), .result = *result};
}

Path requirePath(const NavWorld& navigation, Vec3 from, Vec3 to, const NavQueryFilter& filter = {})
{
    Expected<Path, NavError> path = findPath(navigation, from, to, filter);
    REQUIRE_MESSAGE(path.hasValue(), (path ? std::string() : std::string(navErrorName(path.error()))));
    return std::move(*path);
}

bool near(Vec3 a, Vec3 b, f32 epsilon)
{
    return glm::distance(a, b) <= epsilon;
}

bool passesNear(const Path& path, Vec3 point, f32 epsilon)
{
    return std::ranges::any_of(path.corners,
                               [&](const Vec3& corner) { return near(corner, point, epsilon); });
}

bool sameBits(std::span<const Vec3> a, std::span<const Vec3> b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

NavMeshData bake(const World& world, Entity surface)
{
    Expected<NavGeometry, std::string> geometry = collectNavGeometry(world, surface, {});
    REQUIRE(geometry.hasValue());
    Expected<NavMeshData, std::string> data = bakeNavMesh(*geometry, nullptr);
    REQUIRE(data.hasValue());
    return std::move(*data);
}

void writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
}

// Tiles whose input hash differs between two states of a World.
u32 changedTiles(const NavGeometry& before, const NavGeometry& after, const NavMeshData& data)
{
    u32 changed = 0;
    for (const NavTileData& tile : data.tiles)
    {
        const NavTileCoord coord{.x = tile.x, .z = tile.z};
        changed += navTileInputHash(before, coord) != navTileInputHash(after, coord) ? 1u : 0u;
    }
    return changed;
}

NavGeometry geometryOf(const World& world, Entity surface)
{
    Expected<NavGeometry, std::string> geometry = collectNavGeometry(world, surface, {});
    REQUIRE(geometry.hasValue());
    return std::move(*geometry);
}

// A project directory mounted as `assets` with a streamer over it.
struct Files
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .queueCapacity = 256}};
    std::unique_ptr<AssetStreamer> streamer;

    Files()
    {
        REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
        REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
        streamer = std::make_unique<AssetStreamer>(
            files, jobs, AssetStreamerDesc{.cacheMount = "", .verifySources = true});
    }

    void write(std::string_view name, std::span<const std::byte> bytes)
    {
        writeBytes(directory.path / name, bytes);
    }
};

class CountingSink final : public NavDebugSink
{
public:
    void line(const Vec3& a, const Vec3& b, const Vec4&) override
    {
        ++lines;
        finite = finite && std::isfinite(a.x + a.y + a.z + b.x + b.y + b.z);
    }

    u32 lines = 0;
    bool finite = true;
};

} // namespace

TEST_CASE("a surface without data is baked at load and finds a path around a wall")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    CHECK_FALSE(navigation.isReady(0));
    navigation.waitIdle(nullptr);
    REQUIRE(navigation.isReady(0));
    const NavStatistics& stats = navigation.statistics();
    CHECK(stats.surfaces == 1);
    CHECK(stats.tiles > 0);
    CHECK(stats.polygons > stats.tiles);
    CHECK(stats.pendingTiles == 0);
    CHECK(stats.tileBuilds == 16);
    CHECK(stats.failures == 0);

    const Path path = requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f));
    CHECK_FALSE(path.result.partial);
    CHECK(path.result.count == path.result.total);
    CHECK(path.corners.size() >= 3);
    CHECK(near(path.corners.front(), Vec3(-5.0f, 0.0f, 0.0f), 0.3f));
    CHECK(near(path.corners.back(), Vec3(-5.0f, 0.0f, 6.0f), 0.3f));
    CHECK(path.result.length > 12.0f);
    CHECK(std::ranges::any_of(path.corners, [](const Vec3& c) { return c.x > 0.9f || c.x < -10.9f; }));
}

TEST_CASE("a path climbs a ramp to a platform and crosses a jump link to a box")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);

    const Path ramp = requirePath(navigation, Vec3(0.0f, 0.0f, -12.0f), Vec3(12.0f, 2.0f, -12.0f));
    CHECK_FALSE(ramp.result.partial);
    CHECK(near(ramp.corners.back(), Vec3(12.0f, 2.0f, -12.0f), 0.3f));

    const Path jump = requirePath(navigation, Vec3(4.0f, 0.0f, 12.0f), Vec3(12.0f, 1.5f, 12.0f));
    CHECK_FALSE(jump.result.partial);
    CHECK(near(jump.corners.back(), Vec3(12.0f, 1.5f, 12.0f), 0.3f));
    CHECK(passesNear(jump, Vec3(8.0f, 0.0f, 12.0f), 0.6f));
    CHECK(passesNear(jump, Vec3(11.0f, 1.5f, 12.0f), 0.6f));

    // Without the link's area the box cannot be reached.
    NavQueryFilter noJumps;
    noJumps.areaMask = ~(1u << 2);
    CHECK(requirePath(navigation, Vec3(4.0f, 0.0f, 12.0f), Vec3(12.0f, 1.5f, 12.0f), noJumps).result.partial);
}

TEST_CASE("area costs make a path go around an area and an area mask blocks it")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    const Vec3 from(-15.0f, 0.0f, -2.0f);
    const Vec3 to(-15.0f, 0.0f, -8.0f);

    const Path straight = requirePath(navigation, from, to);
    CHECK(straight.result.length < 6.5f);

    level.world.get<NavMeshSurface>(level.surface).areaCosts[3] = 50.0f;
    level.world.markChanged<NavMeshSurface>();
    navigation.update(0.0f, nullptr, false);
    REQUIRE(navigation.isReady(0));
    const Path around = requirePath(navigation, from, to);
    CHECK_FALSE(around.result.partial);
    CHECK(around.result.length > 25.0f);
    CHECK(navigation.statistics().tileBuilds == 16); // costs need no rebuild

    NavQueryFilter noStrip;
    noStrip.areaMask = ~(1u << 3);
    level.world.get<NavMeshSurface>(level.surface).areaCosts[3] = 1.0f;
    level.world.markChanged<NavMeshSurface>();
    navigation.update(0.0f, nullptr, false);
    CHECK(requirePath(navigation, from, to, noStrip).result.length > 25.0f);

    // The strip of area 5 spans the whole level: without it the far side is unreachable.
    NavQueryFilter noWall;
    noWall.areaMask = ~(1u << 5);
    const Path partial =
        requirePath(navigation, Vec3(-15.0f, 0.0f, 14.5f), Vec3(-15.0f, 0.0f, 19.5f), noWall);
    CHECK(partial.result.partial);
    CHECK(partial.corners.back().z < 16.1f);
    CHECK_FALSE(requirePath(navigation, Vec3(-15.0f, 0.0f, 14.5f), Vec3(-15.0f, 0.0f, 19.5f)).result.partial);
}

TEST_CASE("queries report partial paths, points off the navmesh, missing navmeshes and invalid values")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    std::array<Vec3, 8> corners{};
    CHECK(navigation.findPath(Vec3(0.0f), Vec3(1.0f), corners).error() == NavError::NoNavMesh);
    navigation.waitIdle(nullptr);

    // The small platform is unreachable from the ground: the path ends next to it.
    const Path partial = requirePath(navigation, Vec3(-15.0f, 0.0f, 12.0f), Vec3(-15.0f, 2.0f, 8.0f));
    CHECK(partial.result.partial);
    CHECK(partial.corners.back().y < 0.5f);
    // From the platform's edge nearest the destination, nothing leads nearer.
    CHECK(navigation.findPath(Vec3(-15.0f, 2.0f, 9.0f), Vec3(-15.0f, 0.0f, 12.0f), corners).error() ==
          NavError::NoPath);
    CHECK(requirePath(navigation, Vec3(-15.0f, 2.0f, 8.0f), Vec3(-15.0f, 0.0f, 12.0f)).result.partial);

    CHECK(navigation.findPath(Vec3(0.0f), Vec3(0.0f, 0.0f, 35.0f), corners).error() == NavError::OffMesh);
    CHECK(navigation.nearestPoint(Vec3(0.0f, 30.0f, 0.0f)).error() == NavError::OffMesh);
    NavQueryFilter otherType;
    otherType.agentType = 1;
    CHECK(navigation.nearestPoint(Vec3(0.0f), otherType).error() == NavError::NoNavMesh);
    NavQueryFilter badType;
    badType.agentType = kMaxNavAgentTypes;
    CHECK(navigation.nearestPoint(Vec3(0.0f), badType).error() == NavError::InvalidValue);
    NavQueryFilter badExtents;
    badExtents.searchExtents = Vec3(-1.0f);
    CHECK(navigation.nearestPoint(Vec3(0.0f), badExtents).error() == NavError::InvalidValue);
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    CHECK(navigation.findPath(Vec3(nan), Vec3(1.0f), corners).error() == NavError::InvalidValue);
    CHECK(navigation.findPath(Vec3(0.0f), Vec3(1.0f), std::span<Vec3>()).error() == NavError::InvalidValue);
    CHECK(navigation.raycast(Vec3(0.0f), Vec3(nan)).error() == NavError::InvalidValue);
    CHECK(navigation.randomPoint(Vec3(0.0f), -1.0f, 1).error() == NavError::InvalidValue);

    // A span smaller than the path gets its first corners and the whole path's count and length.
    const Path whole = requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f));
    std::array<Vec3, 1> first{};
    const Expected<NavPathResult, NavError> cut =
        navigation.findPath(Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f), first);
    REQUIRE(cut.hasValue());
    CHECK(cut->count == 1);
    CHECK(cut->total == whole.result.total);
    CHECK(cut->length == doctest::Approx(whole.result.length));
}

TEST_CASE("nearestPoint and raycast follow the navmesh surface and stop at walls")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);

    const Expected<Vec3, NavError> point = navigation.nearestPoint(Vec3(-15.0f, 1.0f, -15.0f));
    REQUIRE(point.hasValue());
    CHECK(near(*point, Vec3(-15.0f, 0.0f, -15.0f), 0.3f));

    const Expected<NavRaycastHit, NavError> wall =
        navigation.raycast(Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f));
    REQUIRE(wall.hasValue());
    CHECK(wall->hit);
    CHECK(wall->point.z > 2.0f);
    CHECK(wall->point.z < 2.6f);
    CHECK(std::abs(wall->normal.z) == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(wall->fraction == doctest::Approx(wall->point.z / 6.0f).epsilon(0.02));

    const Expected<NavRaycastHit, NavError> open =
        navigation.raycast(Vec3(-15.0f, 0.0f, -2.0f), Vec3(-15.0f, 0.0f, -14.0f));
    REQUIRE(open.hasValue());
    CHECK_FALSE(open->hit);
    CHECK(open->fraction == 1.0f);
    CHECK(near(open->point, Vec3(-15.0f, 0.0f, -14.0f), 0.3f));
}

TEST_CASE("randomPoint gives the same reachable point for a seed, inside the radius")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    const Vec3 center(-15.0f, 0.0f, -15.0f);
    std::vector<Vec3> points;
    for (u64 seed = 0; seed < 16; ++seed)
    {
        const Expected<Vec3, NavError> point = navigation.randomPoint(center, 4.0f, seed);
        REQUIRE(point.hasValue());
        const Expected<Vec3, NavError> again = navigation.randomPoint(center, 4.0f, seed);
        REQUIRE(again.hasValue());
        CHECK(std::memcmp(&*point, &*again, sizeof(Vec3)) == 0);
        CHECK(glm::distance(Vec2(point->x, point->z), Vec2(center.x, center.z)) <= 4.0f);
        CHECK(std::abs(point->y) < 0.3f);
        points.push_back(*point);
    }
    CHECK(std::ranges::any_of(points, [&](const Vec3& p) { return !near(p, points.front(), 0.01f); }));
    const Expected<Vec3, NavError> zero = navigation.randomPoint(center, 0.0f, 3);
    REQUIRE(zero.hasValue());
    CHECK(near(*zero, center, 0.3f));
}

TEST_CASE("after waitIdle the same queries give the same results with and without Jobs and from a file")
{
    Level level;
    JobSystem jobs{JobSystemDesc{.workerCount = 4, .queueCapacity = 256}};
    NavWorld serial(level.world, nullptr);
    NavWorld parallel(level.world, nullptr,
                      NavWorldDesc{.maxAgents = 16,
                                   .maxPathPolygons = 256,
                                   .maxPathRequestsPerUpdate = 32,
                                   .maxPathIterations = 512,
                                   .maxNeighbours = 6,
                                   .maxTileBuildsInFlight = 3,
                                   .maxEvents = 64});
    serial.waitIdle(nullptr);
    parallel.waitIdle(&jobs);

    Files files;
    files.write("level.navmesh", encodeNavMeshData(bake(level.world, level.surface)));
    level.world.get<NavMeshSurface>(level.surface).data = AssetRef("assets:/level.navmesh");
    NavWorld loaded(level.world, files.streamer.get());
    loaded.waitIdle(nullptr);
    REQUIRE(loaded.isReady(0));
    CHECK(loaded.statistics().tileBuilds == 0);
    CHECK(loaded.statistics().staleTiles == 0);

    const std::array<std::array<Vec3, 2>, 4> queries{
        {{Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)},
         {Vec3(0.0f, 0.0f, -12.0f), Vec3(12.0f, 2.0f, -12.0f)},
         {Vec3(4.0f, 0.0f, 12.0f), Vec3(12.0f, 1.5f, 12.0f)},
         {Vec3(-18.0f, 0.0f, -18.0f), Vec3(18.0f, 0.0f, 18.0f)}}};
    for (const auto& [from, to] : queries)
    {
        const Path a = requirePath(serial, from, to);
        CHECK(sameBits(a.corners, requirePath(serial, from, to).corners));
        CHECK(sameBits(a.corners, requirePath(parallel, from, to).corners));
        CHECK(sameBits(a.corners, requirePath(loaded, from, to).corners));
    }
    const Expected<Vec3, NavError> a = serial.randomPoint(Vec3(0.0f), 15.0f, 42);
    const Expected<Vec3, NavError> b = loaded.randomPoint(Vec3(0.0f), 15.0f, 42);
    REQUIRE((a.hasValue() && b.hasValue()));
    CHECK(std::memcmp(&*a, &*b, sizeof(Vec3)) == 0);
}

TEST_CASE("a moved wall rebuilds only the tiles it touches, swapped in at the start of the next update")
{
    Level level(true);
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    const NavMeshData data = bake(level.world, level.surface);
    const NavGeometry before = geometryOf(level.world, level.surface);
    const u32 builds = navigation.statistics().tileBuilds;
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length > 12.0f);

    level.moveWall(Vec3(-5.0f, 1.0f, -15.0f));
    const u32 expected = changedTiles(before, geometryOf(level.world, level.surface), data);
    REQUIRE(expected > 0);
    REQUIRE(expected < 16);
    navigation.update(0.016f, nullptr, true);
    CHECK(navigation.statistics().pendingTiles > 0);
    // Built, but not swapped in yet.
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length > 12.0f);
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().tileBuilds - builds == expected);
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length < 6.5f);

    // Nothing changed: nothing is rebuilt.
    level.world.markChanged<Transform>();
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().tileBuilds - builds == expected);
}

TEST_CASE("a removed source and a NavLink added at run time rebuild their tiles")
{
    Level level(true);
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    level.world.destroy(level.wall);
    navigation.waitIdle(nullptr);
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length < 6.5f);

    // A link up to the small platform makes it reachable.
    Transform start;
    start.position = Vec3(-15.0f, 0.0f, 11.0f);
    NavLink link;
    link.end = Vec3(0.0f, 2.0f, -3.0f);
    level.world.create(start, link, EntityGuid{.value = 0x3344});
    navigation.waitIdle(nullptr);
    CHECK_FALSE(requirePath(navigation, Vec3(-15.0f, 0.0f, 13.0f), Vec3(-15.0f, 2.0f, 8.0f)).result.partial);
}

TEST_CASE("a carving obstacle cuts a path once it settled, rebuilding only its tile")
{
    Level level(true);
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    const Vec3 from(5.0f, 0.0f, 1.0f);
    const Vec3 to(5.0f, 0.0f, 9.0f);
    CHECK(requirePath(navigation, from, to).result.length < 8.1f);
    const u32 builds = navigation.statistics().tileBuilds;

    Transform transform;
    transform.position = Vec3(5.0f, 0.0f, 5.0f);
    NavObstacle obstacle;
    obstacle.size = Vec3(3.0f, 2.0f, 3.0f);
    obstacle.carve = true;
    obstacle.settleSeconds = 0.5f;
    level.world.create(transform, obstacle);

    navigation.update(0.25f, nullptr, true);
    navigation.update(0.25f, nullptr, true);
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().tileBuilds == builds); // not settled yet
    navigation.update(0.25f, nullptr, true);
    CHECK(navigation.statistics().pendingTiles == 1);
    CHECK(requirePath(navigation, from, to).result.length < 8.1f);
    navigation.update(0.0f, nullptr, false);
    CHECK(navigation.statistics().tileBuilds == builds + 1);
    const Path around = requirePath(navigation, from, to);
    CHECK(around.result.length > 8.1f);
    CHECK(std::ranges::any_of(around.corners, [](const Vec3& c) { return std::abs(c.x - 5.0f) >= 1.0f; }));
}

TEST_CASE("invalidate rebuilds the tiles of a box with rebuildAtRuntime and marks them stale without")
{
    Level rebuilt(true);
    NavWorld navigation(rebuilt.world, nullptr);
    navigation.waitIdle(nullptr);
    const u32 builds = navigation.statistics().tileBuilds;
    navigation.invalidate(Aabb{.min = Vec3(4.0f, -1.0f, 4.0f), .max = Vec3(5.0f, 1.0f, 5.0f)});
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().tileBuilds == builds + 1);
    navigation.invalidate(Aabb{.min = Vec3(50.0f), .max = Vec3(60.0f)});
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().tileBuilds == builds + 1);

    Level kept;
    NavWorld fixed(kept.world, nullptr);
    fixed.waitIdle(nullptr);
    fixed.invalidate(Aabb{.min = Vec3(4.0f, -1.0f, 4.0f), .max = Vec3(5.0f, 1.0f, 5.0f)});
    fixed.waitIdle(nullptr);
    CHECK(fixed.statistics().tileBuilds == 16);
    CHECK(fixed.statistics().staleTiles == 1);
}

TEST_CASE("stale tiles of a loaded file are counted without rebuildAtRuntime and rebuilt with it")
{
    Level level;
    Files files;
    const NavMeshData data = bake(level.world, level.surface);
    const NavGeometry before = geometryOf(level.world, level.surface);
    files.write("level.navmesh", encodeNavMeshData(data));
    level.world.get<NavMeshSurface>(level.surface).data = AssetRef("assets:/level.navmesh");
    level.moveWall(Vec3(-5.0f, 1.0f, -15.0f));
    const u32 expected = changedTiles(before, geometryOf(level.world, level.surface), data);
    REQUIRE(expected > 0);

    {
        NavWorld navigation(level.world, files.streamer.get());
        navigation.waitIdle(nullptr);
        CHECK(navigation.isReady(0));
        CHECK(navigation.statistics().staleTiles == expected);
        CHECK(navigation.statistics().tileBuilds == 0);
        // The file's wall is still there.
        CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length >
              12.0f);
    }
    level.world.get<NavMeshSurface>(level.surface).rebuildAtRuntime = true;
    NavWorld navigation(level.world, files.streamer.get());
    navigation.waitIdle(&files.jobs);
    CHECK(navigation.statistics().staleTiles == 0);
    CHECK(navigation.statistics().tileBuilds == expected);
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length < 6.5f);
}

TEST_CASE("a rejected or missing .navmesh file is baked at load")
{
    Level level;
    Files files;
    const std::array<std::byte, 16> garbage{};
    files.write("broken.navmesh", garbage);
    level.world.get<NavMeshSurface>(level.surface).data = AssetRef("assets:/broken.navmesh");
    {
        NavWorld navigation(level.world, files.streamer.get());
        navigation.waitIdle(nullptr);
        CHECK(navigation.isReady(0));
        CHECK(navigation.statistics().failures == 1);
        CHECK(navigation.statistics().tileBuilds == 16);
    }
    level.world.get<NavMeshSurface>(level.surface).data = AssetRef("assets:/missing.navmesh");
    NavWorld navigation(level.world, files.streamer.get());
    navigation.waitIdle(nullptr);
    CHECK(navigation.isReady(0));
    CHECK(navigation.statistics().failures == 1);
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length > 12.0f);
}

TEST_CASE("live reload of a .navmesh file replaces the surface's tiles")
{
    Level level;
    Files files;
    files.write("level.navmesh", encodeNavMeshData(bake(level.world, level.surface)));
    level.world.get<NavMeshSurface>(level.surface).data = AssetRef("assets:/level.navmesh");
    NavWorld navigation(level.world, files.streamer.get());
    navigation.waitIdle(nullptr);
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length > 12.0f);

    // The same level baked without its wall.
    level.world.emplace<Inactive>(level.wall);
    files.write("level.navmesh", encodeNavMeshData(bake(level.world, level.surface)));
    level.world.remove<Inactive>(level.wall);
    REQUIRE(files.streamer->reload("assets:/level.navmesh"));
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().tileBuilds == 0);
    CHECK(navigation.statistics().staleTiles > 0);
    CHECK(requirePath(navigation, Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).result.length < 6.5f);
}

TEST_CASE("a Mesh Collider's model is loaded through the streamer before its surface is baked")
{
    Files files;
    World world;
    Transform center;
    center.position = Vec3(0.0f, 2.0f, 0.0f);
    world.create(center, surfaceSettings(false));
    Transform ground;
    ground.rotation = glm::angleAxis(-std::numbers::pi_v<f32> * 0.5f, Vec3(1.0f, 0.0f, 0.0f));
    ground.scale = Vec3(40.0f);
    Collider collider;
    collider.shape = ColliderShape::Mesh;
    collider.mesh = AssetRef("test:/gltf/binary.glb");
    world.create(ground, collider);

    NavWorld navigation(world, files.streamer.get());
    navigation.waitIdle(nullptr);
    REQUIRE(navigation.isReady(0));
    CHECK(navigation.statistics().tiles > 0);
    const Expected<Vec3, NavError> point = navigation.nearestPoint(Vec3(1.0f, 0.5f, 1.0f));
    REQUIRE(point.hasValue());
    CHECK(std::abs(point->y) < 0.3f);
}

TEST_CASE("each agent type has one surface: later ones of a type are ignored")
{
    Level level;
    Transform other;
    other.position = Vec3(0.0f, 2.0f, 0.0f);
    level.world.create(other, surfaceSettings(false));
    NavMeshSurface small = surfaceSettings(false);
    small.agentType = 1;
    small.agentRadius = 1.0f;
    small.size = Vec3(20.0f, 8.0f, 20.0f);
    level.world.create(other, small);
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().surfaces == 2);
    CHECK(navigation.statistics().tileBuilds == 16 + 4);
    CHECK(navigation.isReady(1));
    NavQueryFilter wide;
    wide.agentType = 1;
    // The wider agent keeps a metre from the wall.
    const Expected<NavRaycastHit, NavError> hit =
        navigation.raycast(Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f), wide);
    REQUIRE(hit.hasValue());
    CHECK(hit->point.z > 1.4f);
    CHECK(hit->point.z < 1.9f);

    // Removing the first surface of type 0 makes the second one used.
    level.world.destroy(level.surface);
    navigation.waitIdle(nullptr);
    CHECK(navigation.statistics().surfaces == 2);
    CHECK(navigation.isReady(0));
}

TEST_CASE("setDestination, stop and warp write the NavAgent and its Transform")
{
    Level level;
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    Transform transform;
    transform.position = Vec3(0.0f, 3.0f, -15.0f);
    const Entity agent = level.world.create(transform, NavAgent{});
    REQUIRE(navigation.setDestination(agent, Vec3(1.0f, 0.0f, 2.0f)).hasValue());
    CHECK(*level.world.get<NavAgent>(agent).destination == Vec3(1.0f, 0.0f, 2.0f));
    REQUIRE(navigation.stop(agent).hasValue());
    CHECK_FALSE(level.world.get<NavAgent>(agent).destination.has_value());
    REQUIRE(navigation.warp(agent, Vec3(0.0f, 1.0f, -15.0f)).hasValue());
    CHECK(near(level.world.get<Transform>(agent).position, Vec3(0.0f, 0.0f, -15.0f), 0.3f));
    CHECK(navigation.warp(agent, Vec3(0.0f, 30.0f, 0.0f)).error() == NavError::OffMesh);
    CHECK(navigation.setDestination(agent, Vec3(std::numeric_limits<f32>::infinity())).error() ==
          NavError::InvalidValue);
    CHECK(navigation.setDestination(level.wall, Vec3(0.0f)).error() == NavError::NoAgent);
    level.world.destroy(agent);
    CHECK(navigation.stop(agent).error() == NavError::NoAgent);
    CHECK(navigation.warp(agent, Vec3(0.0f)).error() == NavError::NoAgent);
}

TEST_CASE("drawDebug draws the navmesh by area, tile borders, links and obstacles as selected")
{
    Level level;
    Transform transform;
    transform.position = Vec3(-15.0f, 0.0f, -15.0f);
    NavObstacle box;
    level.world.create(transform, box);
    NavObstacle cylinder;
    cylinder.shape = NavObstacleShape::Cylinder;
    level.world.create(transform, cylinder);
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);

    const auto count = [&](const NavDebugOptions& options) {
        CountingSink sink;
        navigation.drawDebug(options, sink);
        CHECK(sink.finite);
        return sink.lines;
    };
    const NavDebugOptions none{.navMesh = false,
                               .tiles = false,
                               .links = false,
                               .agents = false,
                               .paths = false,
                               .obstacles = false,
                               .agentTypeMask = 0xFFFFFFFFu};
    CHECK(count(none) == 0);
    NavDebugOptions mesh = none;
    mesh.navMesh = true;
    CHECK(count(mesh) >= navigation.statistics().polygons * 3);
    NavDebugOptions tiles = none;
    tiles.tiles = true;
    CHECK(count(tiles) == navigation.statistics().tiles * 4);
    NavDebugOptions links = none;
    links.links = true;
    CHECK(count(links) == 8);
    NavDebugOptions obstacles = none;
    obstacles.obstacles = true;
    CHECK(count(obstacles) == 12 + 16 * 2 + 4);
    mesh.agentTypeMask = 0x2u;
    CHECK(count(mesh) == 0);
}

TEST_CASE("queries and a steady update do not allocate")
{
    Level level(true);
    Transform transform;
    transform.position = Vec3(-15.0f, 0.0f, -15.0f);
    NavObstacle obstacle;
    obstacle.carve = true;
    level.world.create(transform, obstacle);
    NavWorld navigation(level.world, nullptr);
    navigation.waitIdle(nullptr);
    navigation.update(0.016f, nullptr, true);
    std::array<Vec3, 64> corners{};
    CountingSink sink;
    navigation.drawDebug(NavDebugOptions{}, sink); // the first run of a query caches its archetypes
    test::ScopedAllocationCounter counter;
    for (u32 i = 0; i < 4; ++i)
    {
        CHECK(navigation.findPath(Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f), corners).hasValue());
        CHECK(navigation.findPath(Vec3(-15.0f, 0.0f, 12.0f), Vec3(-15.0f, 2.0f, 8.0f), corners).hasValue());
        CHECK(navigation.nearestPoint(Vec3(1.0f, 1.0f, 1.0f)).hasValue());
        CHECK(navigation.raycast(Vec3(-5.0f, 0.0f, 0.0f), Vec3(-5.0f, 0.0f, 6.0f)).hasValue());
        CHECK(navigation.randomPoint(Vec3(0.0f), 10.0f, i).hasValue());
        level.world.markChanged<Transform>();
        navigation.update(0.016f, nullptr, true);
        navigation.drawDebug(NavDebugOptions{}, sink);
    }
    CHECK(counter.count() == 0);
}
