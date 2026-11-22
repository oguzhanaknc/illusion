// The bake input of NavMeshSurfaces and the tiled bake (ADR 0062): every Collider shape as triangles, layer
// masks, body types, Inactive sources, areas, volumes and links; the same bytes for any number of workers;
// tile input hashes and the rebake of stale tiles only.

#include <illusion/assets/Mesh.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavBuild.hpp>
#include <illusion/nav/NavMeshData.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kEpsilon = 1e-4f;

// A surface of @p size around @p center.
Entity addSurface(World& world, Vec3 center = Vec3(0.0f), Vec3 size = Vec3(100.0f, 20.0f, 100.0f))
{
    Transform transform;
    transform.position = center;
    NavMeshSurface surface;
    surface.size = size;
    return world.create(transform, surface);
}

Entity addCollider(World& world, const Collider& collider, const Transform& transform = {},
                   MotionType motion = MotionType::Static, u32 layer = 0)
{
    RigidBody body;
    body.motionType = motion;
    body.layer = layer;
    return world.create(transform, collider, body);
}

Collider boxCollider(Vec3 halfExtents)
{
    Collider collider;
    collider.shape = ColliderShape::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

NavGeometry collect(const World& world, Entity surface, const NavMeshLookup& findMesh = {})
{
    Expected<NavGeometry, std::string> geometry = collectNavGeometry(world, surface, findMesh);
    REQUIRE_MESSAGE(geometry.hasValue(), (geometry ? std::string() : geometry.error()));
    return std::move(*geometry);
}

usize triangleCount(const NavGeometry& geometry)
{
    return geometry.indices.size() / 3;
}

Vec3 corner(const NavGeometry& geometry, usize triangle, u32 index)
{
    return geometry.vertices[geometry.indices[triangle * 3 + index]];
}

Vec3 normalOf(const NavGeometry& geometry, usize triangle)
{
    return glm::cross(corner(geometry, triangle, 1) - corner(geometry, triangle, 0),
                      corner(geometry, triangle, 2) - corner(geometry, triangle, 0));
}

Aabb boundsOf(const NavGeometry& geometry)
{
    Aabb bounds;
    for (const Vec3& vertex : geometry.vertices)
    {
        bounds.expand(vertex);
    }
    return bounds;
}

// Every triangle faces away from @p center.
bool facesOutward(const NavGeometry& geometry, Vec3 center)
{
    for (usize t = 0; t < triangleCount(geometry); ++t)
    {
        const Vec3 centroid =
            (corner(geometry, t, 0) + corner(geometry, t, 1) + corner(geometry, t, 2)) / 3.0f;
        if (glm::dot(normalOf(geometry, t), centroid - center) <= 0.0f)
        {
            return false;
        }
    }
    return true;
}

bool near(Vec3 a, Vec3 b, f32 epsilon = kEpsilon)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec3(epsilon)));
}

// A 40 m square ground on layer 0 with a wall across part of it, and a surface over it.
struct Level
{
    World world;
    Entity surface;
    Entity wall;

    Level()
    {
        surface = addSurface(world, Vec3(0.0f, 2.0f, 0.0f), Vec3(40.0f, 8.0f, 40.0f));
        Transform ground;
        ground.position = Vec3(0.0f, -0.5f, 0.0f);
        addCollider(world, boxCollider(Vec3(20.0f, 0.5f, 20.0f)), ground);
        Transform wallTransform;
        wallTransform.position = Vec3(-5.0f, 1.0f, 3.0f);
        wall = addCollider(world, boxCollider(Vec3(6.0f, 1.0f, 0.25f)), wallTransform);
    }
};

std::vector<u64> tileHashes(const NavGeometry& geometry, const NavMeshData& data)
{
    std::vector<u64> hashes;
    for (const NavTileData& tile : data.tiles)
    {
        hashes.push_back(navTileInputHash(geometry, NavTileCoord{.x = tile.x, .z = tile.z}));
    }
    return hashes;
}

} // namespace

TEST_CASE("a box collider becomes twelve outward triangles, scaled, rotated and offset like physics")
{
    World world;
    const Entity surface = addSurface(world);
    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 3.0f);
    transform.scale = Vec3(2.0f, 1.0f, 3.0f);
    Collider collider = boxCollider(Vec3(1.0f, 0.5f, 1.0f));
    collider.offset = Vec3(0.0f, 1.0f, 0.0f);
    addCollider(world, collider, transform);

    const NavGeometry geometry = collect(world, surface);
    CHECK(triangleCount(geometry) == 12);
    CHECK(geometry.areas == std::vector<u8>(12, 0));
    const Aabb bounds = boundsOf(geometry);
    CHECK(near(bounds.min, Vec3(-1.0f, 2.5f, 0.0f)));
    CHECK(near(bounds.max, Vec3(3.0f, 3.5f, 6.0f)));
    CHECK(facesOutward(geometry, Vec3(1.0f, 3.0f, 3.0f)));

    // A quarter turn about +y swaps the box's x and z extents.
    World turned;
    const Entity turnedSurface = addSurface(turned);
    Transform rotated;
    rotated.rotation = glm::angleAxis(std::numbers::pi_v<f32> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    addCollider(turned, boxCollider(Vec3(2.0f, 0.5f, 1.0f)), rotated);
    const Aabb turnedBounds = boundsOf(collect(turned, turnedSurface));
    CHECK(near(turnedBounds.max, Vec3(1.0f, 0.5f, 2.0f)));
}

TEST_CASE("a negative scale keeps a box's triangles facing outward")
{
    World world;
    const Entity surface = addSurface(world);
    Transform transform;
    transform.scale = Vec3(-1.0f, 1.0f, 1.0f);
    addCollider(world, boxCollider(Vec3(1.0f)), transform);
    const NavGeometry geometry = collect(world, surface);
    CHECK(triangleCount(geometry) == 12);
    CHECK(facesOutward(geometry, Vec3(0.0f)));
}

TEST_CASE("spheres, capsules and cylinders become 16-segment shapes of their size and scale")
{
    World world;
    const Entity surface = addSurface(world);
    Collider sphere;
    sphere.shape = ColliderShape::Sphere;
    sphere.radius = 0.5f;
    Transform sphereTransform;
    sphereTransform.position = Vec3(10.0f, 0.0f, 0.0f);
    sphereTransform.scale = Vec3(2.0f);
    addCollider(world, sphere, sphereTransform);
    const NavGeometry geometry = collect(world, surface);
    REQUIRE(triangleCount(geometry) > 0);
    for (const Vec3& vertex : geometry.vertices)
    {
        CHECK(std::abs(glm::length(vertex - sphereTransform.position) - 1.0f) < kEpsilon);
    }
    CHECK(facesOutward(geometry, sphereTransform.position));
    const Aabb bounds = boundsOf(geometry);
    CHECK(std::abs(bounds.max.y - 1.0f) < kEpsilon);
    CHECK(std::abs(bounds.min.y + 1.0f) < kEpsilon);
    CHECK(std::abs(bounds.max.x - 11.0f) < kEpsilon);

    World capsuleWorld;
    const Entity capsuleSurface = addSurface(capsuleWorld);
    Collider capsule;
    capsule.shape = ColliderShape::Capsule;
    capsule.radius = 0.5f;
    capsule.halfHeight = 1.0f;
    addCollider(capsuleWorld, capsule);
    const NavGeometry capsuleGeometry = collect(capsuleWorld, capsuleSurface);
    REQUIRE(triangleCount(capsuleGeometry) > 0);
    for (const Vec3& vertex : capsuleGeometry.vertices)
    {
        const Vec3 axis(0.0f, std::clamp(vertex.y, -1.0f, 1.0f), 0.0f);
        CHECK(std::abs(glm::length(vertex - axis) - 0.5f) < kEpsilon);
    }
    CHECK(facesOutward(capsuleGeometry, Vec3(0.0f)));
    CHECK(std::abs(boundsOf(capsuleGeometry).max.y - 1.5f) < kEpsilon);

    World cylinderWorld;
    const Entity cylinderSurface = addSurface(cylinderWorld);
    Collider cylinder;
    cylinder.shape = ColliderShape::Cylinder;
    cylinder.radius = 1.0f;
    cylinder.halfHeight = 0.5f;
    addCollider(cylinderWorld, cylinder);
    const NavGeometry cylinderGeometry = collect(cylinderWorld, cylinderSurface);
    CHECK(triangleCount(cylinderGeometry) == 4 * 16);
    for (const Vec3& vertex : cylinderGeometry.vertices)
    {
        const f32 radial = std::sqrt(vertex.x * vertex.x + vertex.z * vertex.z);
        CHECK((std::abs(radial - 1.0f) < kEpsilon || radial < kEpsilon));
        CHECK(std::abs(std::abs(vertex.y) - 0.5f) < kEpsilon);
    }
    CHECK(facesOutward(cylinderGeometry, Vec3(0.0f)));
}

TEST_CASE("mesh and convex hull colliders use the triangles of their mesh through the lookup")
{
    MeshData mesh;
    mesh.vertices.resize(4);
    mesh.vertices[0].position = Vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[1].position = Vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices[2].position = Vec3(1.0f, 0.0f, 1.0f);
    mesh.vertices[3].position = Vec3(1.0f, 0.0f, 0.0f);
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(Submesh{});
    mesh.submeshes[0].indexCount = 6;
    mesh.computeBounds();
    const AssetRef meshRef("game:/floor.glb#mesh/0");
    const NavMeshLookup lookup = [&](const AssetRef& ref, u32) -> const MeshData* {
        return ref == meshRef ? &mesh : nullptr;
    };

    World world;
    const Entity surface = addSurface(world);
    Collider meshCollider;
    meshCollider.shape = ColliderShape::Mesh;
    meshCollider.mesh = meshRef;
    Transform transform;
    transform.position = Vec3(0.0f, 1.0f, 0.0f);
    transform.scale = Vec3(2.0f, 1.0f, 2.0f);
    addCollider(world, meshCollider, transform);
    Collider hull = meshCollider;
    hull.shape = ColliderShape::ConvexHull;
    addCollider(world, hull);
    Collider missing = meshCollider;
    missing.mesh = AssetRef("game:/missing.glb#mesh/0");
    addCollider(world, missing);
    Collider codeMade;
    codeMade.shape = ColliderShape::Mesh;
    addCollider(world, codeMade);

    const NavGeometry geometry = collect(world, surface, lookup);
    REQUIRE(triangleCount(geometry) == 4);
    CHECK(near(corner(geometry, 0, 2), Vec3(2.0f, 1.0f, 2.0f)));
    CHECK(normalOf(geometry, 0).y > 0.0f);
    CHECK(near(corner(geometry, 2, 2), Vec3(1.0f, 0.0f, 1.0f)));
    CHECK(geometry.missingMeshes == 1);
    CHECK(geometry.skippedColliders == 1);
}

TEST_CASE("the bake input reads Static and Kinematic bodies and bare colliders on the surface's layers")
{
    World world;
    const Entity surface = addSurface(world);
    world.get<NavMeshSurface>(surface).layerMask = 0b0101u;
    const Collider box = boxCollider(Vec3(0.5f));
    addCollider(world, box, {}, MotionType::Static, 0);
    addCollider(world, box, {}, MotionType::Kinematic, 2);
    addCollider(world, box, {}, MotionType::Dynamic, 0);
    addCollider(world, box, {}, MotionType::Static, 1);
    world.create(Transform{}, box);
    CHECK(triangleCount(collect(world, surface)) == 3 * 12);

    world.get<NavMeshSurface>(surface).layerMask = 0b0010u;
    CHECK(triangleCount(collect(world, surface)) == 12);
}

TEST_CASE("the bake input skips Inactive Entities and colliders outside the surface's box")
{
    World world;
    const Entity surface = addSurface(world, Vec3(0.0f), Vec3(10.0f));
    const Collider box = boxCollider(Vec3(0.5f));
    world.create(Transform{}, box, Inactive{});
    Transform far;
    far.position = Vec3(30.0f, 0.0f, 0.0f);
    addCollider(world, box, far);
    CHECK(triangleCount(collect(world, surface)) == 0);

    addCollider(world, box);
    CHECK(triangleCount(collect(world, surface)) == 12);
}

TEST_CASE("NavArea marks its collider's triangles, and volumes and carving obstacles become volumes")
{
    World world;
    const Entity surface = addSurface(world);
    const Collider box = boxCollider(Vec3(0.5f));
    const Entity road = addCollider(world, box);
    world.emplace<NavArea>(road, NavArea{.area = 3, .walkable = true, .volume = false, .size = Vec3(1.0f)});
    const Entity pit = addCollider(world, box);
    world.emplace<NavArea>(pit, NavArea{.area = 0, .walkable = false, .volume = false, .size = Vec3(1.0f)});
    Transform volumeTransform;
    volumeTransform.position = Vec3(4.0f, 0.0f, 0.0f);
    world.create(volumeTransform,
                 NavArea{.area = 5, .walkable = true, .volume = true, .size = Vec3(2.0f, 1.0f, 4.0f)});
    Transform crateTransform;
    crateTransform.position = Vec3(-4.0f, 0.0f, 0.0f);
    crateTransform.rotation = glm::angleAxis(0.5f, Vec3(0.0f, 1.0f, 0.0f));
    NavObstacle crate;
    crate.carve = true;
    crate.size = Vec3(2.0f);
    world.create(crateTransform, crate);
    NavObstacle barrel;
    barrel.shape = NavObstacleShape::Cylinder;
    barrel.carve = true;
    barrel.radius = 0.4f;
    barrel.height = 1.2f;
    world.create(Transform{}, barrel);
    world.create(Transform{}, NavObstacle{});

    const NavGeometry geometry = collect(world, surface);
    REQUIRE(triangleCount(geometry) == 24);
    CHECK(std::all_of(geometry.areas.begin(), geometry.areas.begin() + 12, [](u8 a) { return a == 3; }));
    CHECK(std::all_of(geometry.areas.begin() + 12, geometry.areas.end(),
                      [](u8 a) { return a == kNavAreaNotWalkable; }));
    REQUIRE(geometry.volumes.size() == 3);
    CHECK(near(geometry.volumes[0].center, Vec3(4.0f, 0.0f, 0.0f)));
    CHECK(near(geometry.volumes[0].halfExtents, Vec3(1.0f, 0.5f, 2.0f)));
    CHECK(geometry.volumes[0].area == 5);
    CHECK_FALSE(geometry.volumes[0].cylinder);
    CHECK(geometry.volumes[1].area == kNavAreaNotWalkable);
    CHECK(std::abs(geometry.volumes[1].yaw - 0.5f) < kEpsilon);
    CHECK(geometry.volumes[2].cylinder);
    CHECK(near(geometry.volumes[2].halfExtents, Vec3(0.4f, 0.6f, 0.4f)));
}

TEST_CASE("NavLinks become links in world space for the surface's agent type")
{
    World world;
    const Entity surface = addSurface(world);
    world.get<NavMeshSurface>(surface).agentType = 2;
    Transform transform;
    transform.position = Vec3(1.0f, 0.0f, 0.0f);
    transform.rotation = glm::angleAxis(std::numbers::pi_v<f32> * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
    NavLink jump;
    jump.end = Vec3(0.0f, 1.0f, 2.0f);
    jump.kind = NavLinkKind::Ladder;
    jump.bidirectional = false;
    jump.radius = 0.25f;
    jump.area = 4;
    const Entity linked = world.create(transform, jump, EntityGuid{.value = 77});
    NavLink other;
    other.agentTypeMask = 0b0001u;
    world.create(Transform{}, other);

    const NavGeometry geometry = collect(world, surface);
    REQUIRE(geometry.links.size() == 1);
    const NavLinkData& link = geometry.links[0];
    CHECK(near(link.start, Vec3(1.0f, 0.0f, 0.0f)));
    CHECK(near(link.end, Vec3(3.0f, 1.0f, 0.0f)));
    CHECK(link.kind == NavLinkKind::Ladder);
    CHECK_FALSE(link.bidirectional);
    CHECK(link.radius == 0.25f);
    CHECK(link.area == 4);
    CHECK(link.entity == world.get<EntityGuid>(linked));
}

TEST_CASE("collectNavGeometry fails without a NavMeshSurface or with values out of range")
{
    World world;
    const Entity plain = world.create(Transform{});
    CHECK_FALSE(collectNavGeometry(world, plain, {}).hasValue());
    CHECK_FALSE(collectNavGeometry(world, Entity{}, {}).hasValue());

    const Entity surface = addSurface(world);
    world.get<NavMeshSurface>(surface).build.tileSize = 8;
    const Expected<NavGeometry, std::string> small = collectNavGeometry(world, surface, {});
    REQUIRE_FALSE(small.hasValue());
    CHECK(small.error().find("tileSize") != std::string::npos);
    world.get<NavMeshSurface>(surface).build.tileSize = 64;
    world.get<NavMeshSurface>(surface).areaCosts[3] = 0.5f;
    CHECK_FALSE(collectNavGeometry(world, surface, {}).hasValue());
    world.get<NavMeshSurface>(surface).areaCosts[3] = 1.0f;
    world.get<NavMeshSurface>(surface).agentType = kMaxNavAgentTypes;
    CHECK_FALSE(collectNavGeometry(world, surface, {}).hasValue());
}

TEST_CASE("a bake covers the surface with tiles and builds walkable ones")
{
    Level level;
    const NavGeometry geometry = collect(level.world, level.surface);
    const Expected<NavMeshData, std::string> data = bakeNavMesh(geometry, nullptr);
    REQUIRE_MESSAGE(data.hasValue(), (data ? std::string() : data.error()));
    // 40 m over tiles of 64 x 0.15 m = 9.6 m.
    CHECK(data->tilesX == 5);
    CHECK(data->tilesZ == 5);
    CHECK(data->tiles.size() == 25);
    CHECK(near(data->origin, Vec3(-20.0f, -2.0f, -20.0f)));
    CHECK(std::abs(data->tileWorldSize - 9.6f) < kEpsilon);
    for (usize i = 0; i < data->tiles.size(); ++i)
    {
        CAPTURE(i);
        CHECK(data->tiles[i].x == static_cast<i32>(i % 5));
        CHECK(data->tiles[i].z == static_cast<i32>(i / 5));
        // The last column and row lie mostly outside the ground; the others are walkable.
        if (data->tiles[i].x < 4 && data->tiles[i].z < 4)
        {
            CHECK_FALSE(data->tiles[i].detour.empty());
        }
    }
}

TEST_CASE("a bake gives the same bytes for one and four workers and for two bakes")
{
    Level level;
    Transform linkTransform;
    linkTransform.position = Vec3(-5.0f, 0.0f, 1.0f);
    NavLink link;
    link.end = Vec3(0.0f, 0.0f, 4.0f);
    level.world.create(linkTransform, link, EntityGuid{.value = 5});
    const NavGeometry geometry = collect(level.world, level.surface);
    REQUIRE(geometry.links.size() == 1);

    const Expected<NavMeshData, std::string> serial = bakeNavMesh(geometry, nullptr);
    REQUIRE(serial.hasValue());
    JobSystem jobs(JobSystemDesc{.workerCount = 4, .queueCapacity = 4096});
    const Expected<NavMeshData, std::string> parallel = bakeNavMesh(geometry, &jobs);
    const Expected<NavMeshData, std::string> again = bakeNavMesh(geometry, &jobs);
    REQUIRE(parallel.hasValue());
    REQUIRE(again.hasValue());
    const std::vector<std::byte> bytes = encodeNavMeshData(*serial);
    CHECK(bytes == encodeNavMeshData(*parallel));
    CHECK(bytes == encodeNavMeshData(*again));
    CHECK(serial->links == std::vector<EntityGuid>{EntityGuid{.value = 5}});
}

TEST_CASE("a moved box changes only the hashes of its tiles and rebakeStaleTiles rebuilds only those")
{
    Level level;
    const NavGeometry before = collect(level.world, level.surface);
    JobSystem jobs(JobSystemDesc{.workerCount = 2, .queueCapacity = 4096});
    Expected<NavMeshData, std::string> data = bakeNavMesh(before, &jobs);
    REQUIRE(data.hasValue());
    CHECK(tileHashes(before, *data) == [&] {
        std::vector<u64> stored;
        for (const NavTileData& tile : data->tiles)
        {
            stored.push_back(tile.inputHash);
        }
        return stored;
    }());

    level.world.get<Transform>(level.wall).position.x += 1.0f;
    const NavGeometry after = collect(level.world, level.surface);
    const std::vector<u64> oldHashes = tileHashes(before, *data);
    const std::vector<u64> newHashes = tileHashes(after, *data);
    std::vector<NavTileCoord> changed;
    for (usize i = 0; i < data->tiles.size(); ++i)
    {
        if (oldHashes[i] != newHashes[i])
        {
            changed.push_back(NavTileCoord{.x = data->tiles[i].x, .z = data->tiles[i].z});
        }
    }
    // The wall spans x in [-11, 1] (now [-10, 2]) at z = 3: the tiles in columns 0 to 2 of row 2.
    CHECK(changed == std::vector<NavTileCoord>{NavTileCoord{.x = 0, .z = 2}, NavTileCoord{.x = 1, .z = 2},
                                               NavTileCoord{.x = 2, .z = 2}});

    std::vector<NavTileCoord> rebuilt;
    NavMeshData rebaked = *data;
    REQUIRE(rebakeStaleTiles(after, &jobs, rebaked, rebuilt).hasValue());
    CHECK(rebuilt == changed);
    const Expected<NavMeshData, std::string> fresh = bakeNavMesh(after, nullptr);
    REQUIRE(fresh.hasValue());
    CHECK(encodeNavMeshData(rebaked) == encodeNavMeshData(*fresh));

    REQUIRE(rebakeStaleTiles(after, &jobs, rebaked, rebuilt).hasValue());
    CHECK(rebuilt.empty());
}

TEST_CASE("rebakeStaleTiles rebuilds every tile when the settings change and keeps the data on failure")
{
    Level level;
    NavGeometry geometry = collect(level.world, level.surface);
    Expected<NavMeshData, std::string> data = bakeNavMesh(geometry, nullptr);
    REQUIRE(data.hasValue());

    geometry.agent.radius = 0.5f;
    std::vector<NavTileCoord> rebuilt;
    REQUIRE(rebakeStaleTiles(geometry, nullptr, *data, rebuilt).hasValue());
    CHECK(rebuilt.size() == data->tiles.size());
    CHECK(data->agent.radius == 0.5f);

    const std::vector<std::byte> kept = encodeNavMeshData(*data);
    geometry.build.cellSize = 0.0f;
    rebuilt.clear();
    CHECK_FALSE(rebakeStaleTiles(geometry, nullptr, *data, rebuilt).hasValue());
    CHECK(rebuilt.empty());
    CHECK(encodeNavMeshData(*data) == kept);
}

TEST_CASE("bakeNavMesh names the setting out of range")
{
    Level level;
    NavGeometry geometry = collect(level.world, level.surface);
    geometry.agent.height = 0.1f;
    const Expected<NavMeshData, std::string> low = bakeNavMesh(geometry, nullptr);
    REQUIRE_FALSE(low.hasValue());
    CHECK(low.error().find("agentHeight") != std::string::npos);

    geometry = collect(level.world, level.surface);
    geometry.areas[0] = 20;
    CHECK_FALSE(bakeNavMesh(geometry, nullptr).hasValue());
    geometry = collect(level.world, level.surface);
    geometry.indices[0] = 100000;
    CHECK_FALSE(bakeNavMesh(geometry, nullptr).hasValue());
}

TEST_CASE("a surface without colliders bakes empty tiles")
{
    World world;
    const Entity surface = addSurface(world, Vec3(0.0f), Vec3(5.0f));
    const Expected<NavMeshData, std::string> data = bakeNavMesh(collect(world, surface), nullptr);
    REQUIRE(data.hasValue());
    REQUIRE(data->tiles.size() == 1);
    CHECK(data->tiles[0].detour.empty());
    CHECK(data->tiles[0].inputHash != 0);
}
