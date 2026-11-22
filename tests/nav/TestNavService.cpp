// NavWorldService (ADR 0062): the scripting API forwards to its NavWorld, returns every NavError instead of
// asserting, and reports agent status, remaining distance and the events of the last update.

#include "TestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/nav/NavWorldService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

Entity addBox(World& world, Vec3 center, Vec3 halfExtents)
{
    Transform transform;
    transform.position = center;
    Collider collider;
    collider.shape = ColliderShape::Box;
    collider.halfExtents = halfExtents;
    RigidBody body;
    body.motionType = MotionType::Static;
    return world.create(transform, collider, body);
}

// A 40 m ground and a 3 m island raised 2 m above it, which no agent can climb onto.
struct Level
{
    World world;
    NavWorld navigation{world, nullptr};
    NavWorldService service{navigation};
    Entity agent;

    Level()
    {
        Transform center;
        center.position = Vec3(0.0f, 2.0f, 0.0f);
        NavMeshSurface surface;
        surface.size = Vec3(40.0f, 8.0f, 40.0f);
        surface.build.cellSize = 0.2f;
        surface.build.tileSize = 50;
        world.create(center, surface);
        addBox(world, Vec3(0.0f, -0.5f, 0.0f), Vec3(20.0f, 0.5f, 20.0f));
        addBox(world, Vec3(10.0f, 1.0f, 0.0f), Vec3(1.5f, 1.0f, 1.5f));

        Transform start;
        start.position = Vec3(-8.0f, 0.0f, -8.0f);
        NavAgent walker;
        walker.drive = NavDrive::Transform;
        agent = world.create(start, walker);
        navigation.waitIdle(nullptr);
    }
};

NavQueryFilter groundFilter()
{
    return NavQueryFilter{.agentType = 0, .areaMask = 0xFFFFFFFFu, .searchExtents = Vec3(2.0f, 4.0f, 2.0f)};
}

} // namespace

TEST_CASE("NavWorldService answers path and point queries like its NavWorld")
{
    Level level;
    NavService& nav = level.service;
    CHECK(&level.service.navigation() == &level.navigation);
    const NavQueryFilter filter = groundFilter();

    std::array<Vec3, 16> corners{};
    const Expected<NavPathResult, NavError> path =
        nav.findPath(Vec3(-8.0f, 0.0f, -8.0f), Vec3(8.0f, 0.0f, 8.0f), corners, filter);
    REQUIRE(path.hasValue());
    CHECK(path->count >= 2);
    CHECK(path->length == doctest::Approx(22.6f).epsilon(0.05));
    CHECK_FALSE(path->partial);

    const Expected<Vec3, NavError> nearest = nav.nearestPoint(Vec3(3.0f, 1.0f, 3.0f), filter);
    REQUIRE(nearest.hasValue());
    CHECK(nearest->y == doctest::Approx(0.0f).epsilon(0.2));

    const Expected<NavRaycastHit, NavError> ray =
        nav.raycast(Vec3(-8.0f, 0.0f, 0.0f), Vec3(8.0f, 0.0f, 0.0f), filter);
    REQUIRE(ray.hasValue());
    CHECK_FALSE(ray->hit);

    const Expected<Vec3, NavError> first = nav.randomPoint(Vec3(0.0f), 5.0f, 42, filter);
    const Expected<Vec3, NavError> second = nav.randomPoint(Vec3(0.0f), 5.0f, 42, filter);
    REQUIRE(first.hasValue());
    REQUIRE(second.hasValue());
    CHECK(*first == *second);
    CHECK(glm::length(Vec2(first->x, first->z)) <= 5.1f);
}

TEST_CASE("NavWorldService returns every NavError and never asserts")
{
    Level level;
    NavService& nav = level.service;
    const NavQueryFilter filter = groundFilter();
    std::array<Vec3, 8> corners{};
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();

    NavQueryFilter otherType = filter;
    otherType.agentType = 3;
    CHECK(nav.nearestPoint(Vec3(0.0f), otherType).error() == NavError::NoNavMesh);
    CHECK(nav.findPath(Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f), corners, otherType).error() ==
          NavError::NoNavMesh);

    CHECK(nav.nearestPoint(Vec3(0.0f, 30.0f, 0.0f), filter).error() == NavError::OffMesh);
    CHECK(nav.findPath(Vec3(0.0f), Vec3(0.0f, 0.0f, 35.0f), corners, filter).error() == NavError::OffMesh);

    // From the point of the island nearest the destination, nothing leads nearer.
    const Expected<NavPathResult, NavError> toIsland =
        nav.findPath(Vec3(-8.0f, 0.0f, 0.0f), Vec3(10.0f, 2.0f, 0.0f), corners, filter);
    REQUIRE(toIsland.hasValue());
    REQUIRE(toIsland->partial);
    CHECK(nav.findPath(corners[toIsland->count - 1], Vec3(10.0f, 2.0f, 0.0f), corners, filter).error() ==
          NavError::NoPath);

    CHECK(nav.findPath(Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f), std::span<Vec3>{}, filter).error() ==
          NavError::InvalidValue);
    CHECK(nav.findPath(Vec3(nan), Vec3(1.0f, 0.0f, 0.0f), corners, filter).error() == NavError::InvalidValue);
    CHECK(nav.nearestPoint(Vec3(nan), filter).error() == NavError::InvalidValue);
    CHECK(nav.raycast(Vec3(0.0f), Vec3(nan), filter).error() == NavError::InvalidValue);
    CHECK(nav.randomPoint(Vec3(0.0f), -1.0f, 1, filter).error() == NavError::InvalidValue);
    NavQueryFilter badType = filter;
    badType.agentType = 99;
    CHECK(nav.nearestPoint(Vec3(0.0f), badType).error() == NavError::InvalidValue);
    CHECK(nav.setDestination(level.agent, Vec3(nan)).error() == NavError::InvalidValue);
    CHECK(nav.warp(level.agent, Vec3(nan)).error() == NavError::InvalidValue);

    const Entity noAgent = level.world.create(Transform{});
    CHECK(nav.setDestination(noAgent, Vec3(0.0f)).error() == NavError::NoAgent);
    CHECK(nav.stop(noAgent).error() == NavError::NoAgent);
    CHECK(nav.warp(noAgent, Vec3(0.0f)).error() == NavError::NoAgent);
    CHECK(nav.status(noAgent).error() == NavError::NoAgent);
    CHECK(nav.remainingDistance(noAgent).error() == NavError::NoAgent);
    CHECK(nav.status(Entity{}).error() == NavError::NoAgent);
    level.world.destroy(level.agent);
    CHECK(nav.status(level.agent).error() == NavError::NoAgent);
    CHECK(nav.remainingDistance(level.agent).error() == NavError::NoAgent);
    CHECK(nav.warp(level.agent, Vec3(0.0f)).error() == NavError::NoAgent);
}

TEST_CASE("NavWorldService sends an agent, reports its status and distance, and hands out the events")
{
    Level level;
    NavService& nav = level.service;
    World& world = level.world;
    CHECK(nav.status(level.agent).valueOr(NavAgentStatus::Failed) == NavAgentStatus::Idle);
    CHECK(nav.remainingDistance(level.agent).valueOr(-1.0f) == 0.0f);

    const Vec3 destination(8.0f, 0.0f, 8.0f);
    REQUIRE(nav.setDestination(level.agent, destination).hasValue());
    CHECK(world.get<NavAgent>(level.agent).destination == destination);

    // An agent added since the last update has no state yet.
    NavAgent fresh;
    fresh.destination = destination;
    const Entity late = world.create(Transform{}, fresh);
    CHECK(nav.status(late).valueOr(NavAgentStatus::Failed) == NavAgentStatus::Pending);
    CHECK(nav.remainingDistance(late).valueOr(-1.0f) == 0.0f);

    bool moving = false;
    f32 first = 0.0f;
    u32 arrivals = 0;
    NavEvent arrival;
    for (u32 i = 0; i < 60 * 60 && arrivals == 0; ++i)
    {
        level.navigation.update(kStep, nullptr, true);
        if (!moving && nav.status(level.agent).valueOr(NavAgentStatus::Failed) == NavAgentStatus::Moving)
        {
            moving = true;
            first = nav.remainingDistance(level.agent).valueOr(0.0f);
        }
        std::array<NavEvent, 4> events{};
        const u32 total = nav.pollEvents(events);
        CHECK(total == level.navigation.events().size());
        for (u32 j = 0; j < total && j < events.size(); ++j)
        {
            if (events[j].agent == level.agent && events[j].type == NavEventType::Arrived)
            {
                ++arrivals;
                arrival = events[j];
            }
        }
    }
    CHECK(moving);
    CHECK(first > 15.0f);
    REQUIRE(arrivals == 1);
    CHECK(nav.status(level.agent).valueOr(NavAgentStatus::Failed) == NavAgentStatus::Arrived);
    CHECK(nav.remainingDistance(level.agent).valueOr(-1.0f) < 0.5f);
    CHECK(glm::distance(arrival.position, destination) < 0.5f);

    REQUIRE(nav.warp(level.agent, Vec3(-8.0f, 0.0f, -8.0f)).hasValue());
    CHECK(glm::distance(world.get<Transform>(level.agent).position, Vec3(-8.0f, 0.0f, -8.0f)) < 0.5f);
    REQUIRE(nav.stop(level.agent).hasValue());
    CHECK_FALSE(world.get<NavAgent>(level.agent).destination.has_value());
}
