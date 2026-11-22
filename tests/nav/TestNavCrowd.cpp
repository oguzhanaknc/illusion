// The crowd of NavWorld (ADR 0062): crowd slots and NavAgentState, the four drives, avoidance, link
// traversal with its events, arrival, failures, OffMesh and replanning, stop and warp, Inactive agents,
// saved destinations, the GameSession's Navigation step through physics, determinism for any number of
// workers and a steady update without allocations.

#include "TestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;
constexpr f64 kSessionStep = 1.0 / 60.0;

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

// A 40 m ground with a wall that has a 2.4 m doorway at x = 0 across z = 0, a ramp up to a platform with a
// 0.25 m step in front of it, and a jump link from the ground onto a box.
void buildLevel(World& world)
{
    Transform center;
    center.position = Vec3(0.0f, 2.0f, 0.0f);
    NavMeshSurface surface;
    surface.size = Vec3(40.0f, 8.0f, 40.0f);
    surface.build.cellSize = 0.2f;
    surface.build.tileSize = 50;
    world.create(center, surface);
    addBox(world, Vec3(0.0f, -0.5f, 0.0f), Vec3(20.0f, 0.5f, 20.0f));
    addBox(world, Vec3(-11.2f, 1.0f, 0.0f), Vec3(10.0f, 1.0f, 0.25f));
    addBox(world, Vec3(11.2f, 1.0f, 0.0f), Vec3(10.0f, 1.0f, 0.25f));

    addBox(world, Vec3(15.0f, 1.0f, -12.0f), Vec3(3.0f, 1.0f, 3.0f));
    const f32 angle = std::atan2(2.0f, 6.0f);
    const Vec3 normal(-std::sin(angle), std::cos(angle), 0.0f);
    addBox(world, Vec3(9.0f, 1.0f, -12.0f) - normal * 0.1f, Vec3(std::sqrt(40.0f) * 0.5f, 0.1f, 1.5f),
           glm::angleAxis(angle, Vec3(0.0f, 0.0f, 1.0f)));
    addBox(world, Vec3(4.0f, 0.125f, -12.0f), Vec3(1.0f, 0.125f, 1.5f));

    addBox(world, Vec3(15.0f, 0.75f, 12.0f), Vec3(2.5f, 0.75f, 2.5f));
}

Entity addJumpLink(World& world)
{
    Transform start;
    start.position = Vec3(10.0f, 0.0f, 12.0f);
    NavLink jump;
    jump.end = Vec3(3.0f, 1.5f, 0.0f);
    jump.height = 1.0f;
    jump.area = 2;
    return world.create(start, jump, EntityGuid{.value = 0x7001});
}

Entity addAgent(World& world, Vec3 position, NavDrive drive, std::optional<Vec3> destination,
                f32 radius = 0.3f)
{
    Transform transform;
    transform.position = position;
    NavAgent agent;
    agent.drive = drive;
    agent.radius = radius;
    agent.destination = destination;
    return world.create(transform, agent);
}

Vec3 positionOf(const World& world, Entity entity)
{
    return world.get<Transform>(entity).position;
}

f32 flatDistance(Vec3 a, Vec3 b)
{
    return glm::length(Vec2(a.x - b.x, a.z - b.z));
}

NavAgentStatus statusOf(const World& world, Entity entity)
{
    return world.get<NavAgentState>(entity).status;
}

// Updates until @p done or @p limit updates; returns the updates run.
template <class Done>
u32 runUntil(NavWorld& navigation, JobSystem* jobs, u32 limit, Done&& done)
{
    for (u32 i = 0; i < limit; ++i)
    {
        navigation.update(kStep, jobs, true);
        if (done())
        {
            return i + 1;
        }
    }
    return limit;
}

u32 countEvents(const NavWorld& navigation, Entity agent, NavEventType type)
{
    return static_cast<u32>(std::ranges::count_if(navigation.events(), [&](const NavEvent& event) {
        return event.agent == agent && event.type == type;
    }));
}

// The bits of every agent's position and rotation, for comparing runs.
u64 agentsHash(World& world)
{
    u64 hash = 1469598103934665603ull;
    world.query<const NavAgent, const Transform>().each(
        [&](Entity entity, const NavAgent&, const Transform& t) {
            const std::array<f32, 7> values{t.position.x, t.position.y, t.position.z, t.rotation.x,
                                            t.rotation.y, t.rotation.z, t.rotation.w};
            hash = (hash ^ entity.index) * 1099511628211ull;
            for (const f32 value : values)
            {
                u32 bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                hash = (hash ^ bits) * 1099511628211ull;
            }
        });
    return hash;
}

// A session with fixed steps over an empty project, for the Navigation step through physics.
struct SessionFixture
{
    SessionFixture()
    {
        reflectCoreComponents();
        reflectPhysicsComponents();
        registerNavComponents(ComponentRegistry::instance());
        REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
        GameSessionDesc desc;
        desc.jobs = &jobs;
        desc.files = &files;
        desc.audioOutput = AudioOutput::Offline;
        desc.fixedStep = true;
        auto created = GameSession::create(desc);
        REQUIRE(created.hasValue());
        session = std::move(*created);
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    std::unique_ptr<GameSession> session;
};

Entity addCharacter(World& world, Vec3 position, Vec3 destination)
{
    const Entity agent = addAgent(world, position, NavDrive::Character, destination);
    world.set(agent, CharacterController{});
    return agent;
}

} // namespace

TEST_CASE("a Transform agent walks to its destination on the navmesh, faces its movement and arrives once")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Vec3 destination(-4.0f, 0.0f, 6.0f);
    const Entity agent = addAgent(world, Vec3(-6.0f, 0.0f, -6.0f), NavDrive::Transform, destination);

    navigation.update(kStep, nullptr, true);
    REQUIRE(world.has<NavAgentState>(agent));
    const std::optional<u32> slot = navigation.findSlot(agent);
    REQUIRE(slot.has_value());
    CHECK(world.get<NavAgentState>(agent).slot == *slot);
    CHECK(navigation.statistics().agents == 1);

    u32 arrived = 0;
    f32 remaining = std::numeric_limits<f32>::max();
    bool shrinking = true;
    bool onMesh = true;
    bool facing = true;
    bool cornersSeen = false;
    for (u32 i = 0; i < 1200 && statusOf(world, agent) != NavAgentStatus::Arrived; ++i)
    {
        navigation.update(kStep, nullptr, true);
        arrived += countEvents(navigation, agent, NavEventType::Arrived);
        const NavAgentState& state = world.get<NavAgentState>(agent);
        const Transform& transform = world.get<Transform>(agent);
        if (state.status == NavAgentStatus::Moving)
        {
            CHECK(state.slot == *slot);
            shrinking = shrinking && state.remainingDistance <= remaining + 0.05f;
            remaining = state.remainingDistance;
            cornersSeen = cornersSeen || !navigation.pathCorners(*slot).empty();
            // The wall is passed through the doorway: the agent never stands inside it.
            CHECK((std::abs(transform.position.z) > 0.5f || std::abs(transform.position.x) < 1.2f));
            if (state.speed > 1.0f)
            {
                const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
                facing =
                    facing && glm::dot(Vec2(forward.x, forward.z), Vec2(state.velocity.x, state.velocity.z)) /
                                      state.speed >
                                  0.5f;
            }
        }
        const Expected<Vec3, NavError> nearest = navigation.nearestPoint(transform.position);
        onMesh = onMesh && nearest && flatDistance(*nearest, transform.position) < 1e-3f &&
                 std::abs(nearest->y - transform.position.y) < 0.2f;
    }
    CHECK(statusOf(world, agent) == NavAgentStatus::Arrived);
    CHECK(arrived == 1);
    CHECK(shrinking);
    CHECK(onMesh);
    CHECK(facing);
    CHECK(cornersSeen);
    CHECK(flatDistance(positionOf(world, agent), destination) <= 0.1f + 1e-3f);
    CHECK(world.get<NavAgentState>(agent).remainingDistance == 0.0f);

    // Standing at the destination: no more events.
    runUntil(navigation, nullptr, 30, [] { return false; });
    CHECK(countEvents(navigation, agent, NavEventType::Arrived) == 0);
    CHECK(world.get<NavAgentState>(agent).speed < 0.05f);
}

TEST_CASE("a RootMotion agent is turned and gets a speed, a None agent only gets its path and velocity")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Vec3 start(-6.0f, 0.0f, -6.0f);
    const Entity rooted = addAgent(world, start, NavDrive::RootMotion, Vec3(6.0f, 0.0f, -6.0f));
    const Entity planned =
        addAgent(world, Vec3(-6.0f, 0.0f, -12.0f), NavDrive::None, Vec3(-6.0f, 0.0f, -18.0f));
    runUntil(navigation, nullptr, 60, [] { return false; });

    // Root motion moves it, not navigation: it turned towards +x.
    const Transform& root = world.get<Transform>(rooted);
    CHECK(root.position == start);
    const Vec3 forward = root.rotation * Vec3(0.0f, 0.0f, 1.0f);
    CHECK(forward.x > 0.9f);
    CHECK(statusOf(world, rooted) == NavAgentStatus::Moving);
    CHECK(world.get<NavAgentState>(rooted).speed > 1.0f);

    // Game code moves a None agent; it follows where the game put it and arrives.
    CHECK(positionOf(world, planned) == Vec3(-6.0f, 0.0f, -12.0f));
    CHECK(world.get<NavAgentState>(planned).velocity.z < -1.0f);
    const u32 updates = runUntil(navigation, nullptr, 600, [&] {
        Transform& transform = world.get<Transform>(planned);
        transform.position += world.get<NavAgentState>(planned).velocity * kStep;
        world.markChanged<Transform>();
        return statusOf(world, planned) == NavAgentStatus::Arrived;
    });
    CHECK(updates < 600);
    CHECK(flatDistance(positionOf(world, planned), Vec3(-6.0f, 0.0f, -18.0f)) < 0.2f);
}

TEST_CASE("two agents crossing each other's paths do not touch and both arrive")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Entity a = addAgent(world, Vec3(-6.0f, 0.0f, 8.0f), NavDrive::Transform, Vec3(6.0f, 0.0f, 8.0f));
    const Entity b = addAgent(world, Vec3(6.0f, 0.0f, 8.0f), NavDrive::Transform, Vec3(-6.0f, 0.0f, 8.0f));
    f32 closest = std::numeric_limits<f32>::max();
    runUntil(navigation, nullptr, 1200, [&] {
        closest = std::min(closest, flatDistance(positionOf(world, a), positionOf(world, b)));
        return statusOf(world, a) == NavAgentStatus::Arrived && statusOf(world, b) == NavAgentStatus::Arrived;
    });
    CHECK(closest >= 0.6f);
    CHECK(statusOf(world, a) == NavAgentStatus::Arrived);
    CHECK(statusOf(world, b) == NavAgentStatus::Arrived);
}

TEST_CASE("a crowd of 50 agents passes through a doorway and arrives")
{
    World world;
    buildLevel(world);
    JobSystem jobs{JobSystemDesc{4, 1024}};
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(&jobs);
    std::array<Entity, 50> agents{};
    // The first through the doorway go farthest, so that those who arrived do not block the others.
    for (u32 i = 0; i < agents.size(); ++i)
    {
        const f32 x = static_cast<f32>(i % 10) * 1.5f - 6.75f;
        const auto row = static_cast<f32>(i / 10);
        agents[i] = addAgent(world, Vec3(x, 0.0f, -4.0f - row * 1.5f), NavDrive::Transform,
                             Vec3(x, 0.0f, 4.0f + (4.0f - row) * 1.5f));
    }
    const u32 updates = runUntil(navigation, &jobs, 60 * 60, [&] {
        return std::ranges::all_of(
            agents, [&](Entity agent) { return statusOf(world, agent) == NavAgentStatus::Arrived; });
    });
    u32 arrived = 0;
    for (const Entity agent : agents)
    {
        arrived += statusOf(world, agent) == NavAgentStatus::Arrived ? 1u : 0u;
    }
    CHECK(arrived == 50);
    CHECK(updates < 60 * 60);
    for (const Entity agent : agents)
    {
        CHECK(positionOf(world, agent).z > 0.5f);
    }
}

TEST_CASE("a jump link moves the agent along its parabola between LinkEntered and LinkExited")
{
    World world;
    buildLevel(world);
    const Entity link = addJumpLink(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Vec3 destination(15.0f, 1.5f, 12.0f);
    const Entity agent = addAgent(world, Vec3(6.0f, 0.0f, 12.0f), NavDrive::Transform, destination);

    u32 entered = 0;
    u32 exited = 0;
    u32 linkUpdates = 0;
    f32 highest = 0.0f;
    runUntil(navigation, nullptr, 900, [&] {
        for (const NavEvent& event : navigation.events())
        {
            if (event.agent != agent)
            {
                continue;
            }
            if (event.type == NavEventType::LinkEntered || event.type == NavEventType::LinkExited)
            {
                CHECK(event.link == link);
                CHECK(event.kind == NavLinkKind::Jump);
            }
            entered += event.type == NavEventType::LinkEntered ? 1u : 0u;
            exited += event.type == NavEventType::LinkExited ? 1u : 0u;
        }
        const NavAgentState& state = world.get<NavAgentState>(agent);
        if (!state.link.isNull())
        {
            CHECK(state.link == link);
            ++linkUpdates;
            highest = std::max(highest, positionOf(world, agent).y);
        }
        return state.status == NavAgentStatus::Arrived;
    });
    CHECK(entered == 1);
    CHECK(exited == 1);
    CHECK(linkUpdates > 10);
    // The apex is 1 m above the higher end.
    CHECK(highest > 2.3f);
    CHECK(highest < 2.6f);
    CHECK(statusOf(world, agent) == NavAgentStatus::Arrived);
    CHECK(flatDistance(positionOf(world, agent), destination) < 0.2f);
    CHECK(positionOf(world, agent).y == doctest::Approx(1.5f).epsilon(0.05));
}

TEST_CASE("an unreachable destination fails with PathFailed and a partial path ends near it")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Entity lost =
        addAgent(world, Vec3(-6.0f, 0.0f, -6.0f), NavDrive::Transform, Vec3(0.0f, 30.0f, 0.0f));
    navigation.update(kStep, nullptr, true);
    CHECK(statusOf(world, lost) == NavAgentStatus::Failed);
    CHECK(countEvents(navigation, lost, NavEventType::PathFailed) == 1);
    navigation.update(kStep, nullptr, true);
    CHECK(countEvents(navigation, lost, NavEventType::PathFailed) == 0);

    // Without a link the top of the box cannot be reached.
    REQUIRE(navigation.setDestination(lost, Vec3(15.0f, 1.5f, 12.0f)).hasValue());
    navigation.update(kStep, nullptr, true);
    CHECK(statusOf(world, lost) == NavAgentStatus::Partial);
    runUntil(navigation, nullptr, 900, [] { return false; });
    CHECK(statusOf(world, lost) == NavAgentStatus::Partial);
    // It stopped at the end of the path, on the ground beside the box.
    CHECK(flatDistance(positionOf(world, lost), Vec3(15.0f, 0.0f, 12.0f)) < 3.6f);
    CHECK(positionOf(world, lost).y < 0.5f);
    CHECK(world.get<NavAgentState>(lost).speed < 0.05f);
}

TEST_CASE("an agent far from the navmesh is OffMesh and replans when it returns")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Entity agent = addAgent(world, Vec3(-6.0f, 6.0f, -6.0f), NavDrive::None, Vec3(-6.0f, 0.0f, -12.0f));
    navigation.update(kStep, nullptr, true);
    navigation.update(kStep, nullptr, true);
    CHECK(statusOf(world, agent) == NavAgentStatus::OffMesh);
    CHECK(world.get<NavAgentState>(agent).speed == 0.0f);

    world.get<Transform>(agent).position = Vec3(-6.0f, 0.0f, -6.0f);
    world.markChanged<Transform>();
    runUntil(navigation, nullptr, 3, [] { return false; });
    CHECK(statusOf(world, agent) == NavAgentStatus::Moving);
    CHECK(world.get<NavAgentState>(agent).remainingDistance > 5.0f);
}

TEST_CASE("stop slows an agent to a stop and warp moves it and searches its path again")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Entity agent =
        addAgent(world, Vec3(-6.0f, 0.0f, -6.0f), NavDrive::Transform, Vec3(-6.0f, 0.0f, -18.0f));
    runUntil(navigation, nullptr, 60, [] { return false; });
    CHECK(world.get<NavAgentState>(agent).speed > 2.0f);

    REQUIRE(navigation.stop(agent).hasValue());
    navigation.update(kStep, nullptr, true);
    CHECK(statusOf(world, agent) == NavAgentStatus::Idle);
    const f32 braking = world.get<NavAgentState>(agent).speed;
    CHECK(braking > 0.0f);
    runUntil(navigation, nullptr, 60, [] { return false; });
    CHECK(world.get<NavAgentState>(agent).speed == 0.0f);
    const Vec3 stopped = positionOf(world, agent);
    navigation.update(kStep, nullptr, true);
    CHECK(glm::distance(positionOf(world, agent), stopped) < 1e-4f);

    REQUIRE(navigation.setDestination(agent, Vec3(-6.0f, 0.0f, -18.0f)).hasValue());
    runUntil(navigation, nullptr, 10, [] { return false; });
    REQUIRE(navigation.warp(agent, Vec3(6.0f, 0.5f, -6.0f)).hasValue());
    CHECK(flatDistance(positionOf(world, agent), Vec3(6.0f, 0.0f, -6.0f)) < 0.01f);
    navigation.update(kStep, nullptr, true);
    CHECK(flatDistance(positionOf(world, agent), Vec3(6.0f, 0.0f, -6.0f)) < 0.2f);
    const u32 updates =
        runUntil(navigation, nullptr, 900, [&] { return statusOf(world, agent) == NavAgentStatus::Arrived; });
    CHECK(updates < 900);
    CHECK(flatDistance(positionOf(world, agent), Vec3(-6.0f, 0.0f, -18.0f)) < 0.2f);
}

TEST_CASE("Inactive agents keep their slot and do not move; removed agents lose their state")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Entity agent =
        addAgent(world, Vec3(-6.0f, 0.0f, -6.0f), NavDrive::Transform, Vec3(-6.0f, 0.0f, -18.0f));
    runUntil(navigation, nullptr, 30, [] { return false; });
    const std::optional<u32> slot = navigation.findSlot(agent);
    REQUIRE(slot.has_value());
    world.set(agent, Inactive{});
    const Vec3 frozen = positionOf(world, agent);
    runUntil(navigation, nullptr, 30, [] { return false; });
    CHECK(positionOf(world, agent) == frozen);
    CHECK(navigation.findSlot(agent) == slot);
    CHECK(world.get<NavAgentState>(agent).speed == 0.0f);

    world.remove<Inactive>(agent);
    runUntil(navigation, nullptr, 30, [] { return false; });
    CHECK(positionOf(world, agent).z < frozen.z - 0.5f);

    world.remove<NavAgent>(agent);
    navigation.update(kStep, nullptr, true);
    CHECK_FALSE(world.has<NavAgentState>(agent));
    CHECK_FALSE(navigation.findSlot(agent).has_value());
    CHECK(navigation.statistics().agents == 0);
}

TEST_CASE("a saved game restores an agent's destination, which it walks to after the load")
{
    reflectCoreComponents();
    registerNavComponents(ComponentRegistry::instance());
    registerSaveComponents();
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    const Entity agent =
        addAgent(world, Vec3(-6.0f, 0.0f, -6.0f), NavDrive::Transform, Vec3(-6.0f, 0.0f, -18.0f));
    world.set(agent, Saved{});
    world.set(agent, EntityGuid{.value = 0x5a7e});
    runUntil(navigation, nullptr, 20, [] { return false; });
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());

    REQUIRE(navigation.stop(agent).hasValue());
    runUntil(navigation, nullptr, 60, [] { return false; });
    REQUIRE(applySave(world, captured->world).hasValue());
    Entity loaded{};
    world.query<const NavAgent, const EntityGuid>().each(
        [&](Entity entity, const NavAgent&, const EntityGuid& guid) {
            if (guid.value == 0x5a7e)
            {
                loaded = entity;
            }
        });
    REQUIRE_FALSE(loaded.isNull());
    REQUIRE(world.get<NavAgent>(loaded).destination.has_value());
    const u32 updates = runUntil(navigation, nullptr, 900,
                                 [&] { return statusOf(world, loaded) == NavAgentStatus::Arrived; });
    CHECK(updates < 900);
    CHECK(flatDistance(positionOf(world, loaded), Vec3(-6.0f, 0.0f, -18.0f)) < 0.2f);
}

TEST_CASE("the same crowd after 600 fixed steps is identical with no Jobs, 1 and 4 workers")
{
    const auto run = [](u32 workers) {
        World world;
        buildLevel(world);
        addJumpLink(world);
        std::unique_ptr<JobSystem> jobs =
            workers > 0 ? std::make_unique<JobSystem>(JobSystemDesc{workers, 1024}) : nullptr;
        NavWorld navigation(world, nullptr);
        navigation.waitIdle(jobs.get());
        for (u32 i = 0; i < 40; ++i)
        {
            const f32 x = static_cast<f32>(i % 8) * 1.5f - 5.0f;
            const f32 z = static_cast<f32>(i / 8) * 1.5f + 2.0f;
            const bool across = i % 2 == 0;
            const NavDrive drive = i % 5 == 4 ? NavDrive::RootMotion : NavDrive::Transform;
            const Entity agent = addAgent(world, Vec3(x, 0.0f, across ? z : -z), drive,
                                          across ? Vec3(-x, 0.0f, -z - 2.0f) : Vec3(15.0f, 1.5f, 12.0f));
            NavAgent& values = world.get<NavAgent>(agent);
            values.avoidance = static_cast<NavAvoidance>(i % 4);
            values.priority = static_cast<u8>(i % 3 * 20);
        }
        u32 events = 0;
        for (u32 step = 0; step < 600; ++step)
        {
            navigation.update(kStep, jobs.get(), true);
            events += static_cast<u32>(navigation.events().size());
        }
        return std::array<u64, 2>{agentsHash(world), events};
    };
    const std::array<u64, 2> serial = run(0);
    const std::array<u64, 2> one = run(1);
    const std::array<u64, 2> four = run(4);
    CHECK(serial[1] > 0);
    CHECK(one[0] == serial[0]);
    CHECK(four[0] == serial[0]);
    CHECK(one[1] == serial[1]);
    CHECK(four[1] == serial[1]);
}

TEST_CASE("a steady crowd update does not allocate")
{
    World world;
    buildLevel(world);
    JobSystem jobs{JobSystemDesc{4, 1024}};
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(&jobs);
    for (u32 i = 0; i < 50; ++i)
    {
        const f32 x = static_cast<f32>(i % 10) * 1.2f - 6.0f;
        const f32 z = static_cast<f32>(i / 10) * 1.2f + 6.0f;
        addAgent(world, Vec3(x, 0.0f, z), NavDrive::Transform, Vec3(-x, 0.0f, -z - 4.0f));
    }
    runUntil(navigation, &jobs, 30, [] { return false; });
    REQUIRE(navigation.statistics().moving > 40);
    test::ScopedAllocationCounter counter;
    for (u32 i = 0; i < 20; ++i)
    {
        navigation.update(kStep, &jobs, true);
    }
    CHECK(counter.count() == 0);
    CHECK(navigation.statistics().moving > 40);
}

TEST_CASE("drawDebug draws agent cylinders, velocities and path corners")
{
    World world;
    buildLevel(world);
    NavWorld navigation(world, nullptr);
    navigation.waitIdle(nullptr);
    addAgent(world, Vec3(-6.0f, 0.0f, -6.0f), NavDrive::Transform, Vec3(-4.0f, 0.0f, 6.0f));
    runUntil(navigation, nullptr, 10, [] { return false; });

    struct Sink final : NavDebugSink
    {
        void line(const Vec3&, const Vec3&, const Vec4&) override { ++lines; }
        u32 lines = 0;
    };
    const auto count = [&](bool agents, bool paths) {
        Sink sink;
        navigation.drawDebug(NavDebugOptions{.navMesh = false,
                                             .tiles = false,
                                             .links = false,
                                             .agents = agents,
                                             .paths = paths,
                                             .obstacles = false,
                                             .agentTypeMask = 0xFFFFFFFFu},
                             sink);
        return sink.lines;
    };
    CHECK(count(false, false) == 0);
    CHECK(count(true, false) == 12 + 2);
    CHECK(count(false, true) >= 1);
}

TEST_CASE("the Navigation step walks a Character agent up a step and a ramp through physics")
{
    SessionFixture fixture;
    World& world = fixture.session->world();
    buildLevel(world);
    const Vec3 destination(15.0f, 2.0f, -12.0f);
    const Entity agent = addCharacter(world, Vec3(0.0f, 0.0f, -12.0f), destination);
    fixture.session->start();
    u32 updates = 0;
    f32 highest = 0.0f;
    for (; updates < 60 * 20 &&
           !(world.has<NavAgentState>(agent) && statusOf(world, agent) == NavAgentStatus::Arrived);
         ++updates)
    {
        fixture.session->update(kSessionStep);
        highest = std::max(highest, positionOf(world, agent).y);
    }
    CHECK(statusOf(world, agent) == NavAgentStatus::Arrived);
    CHECK(flatDistance(positionOf(world, agent), destination) < 0.3f);
    CHECK(positionOf(world, agent).y == doctest::Approx(2.0f).epsilon(0.05));
    CHECK(highest < 2.2f); // walked, not teleported over the step
    for (u32 i = 0; i < 60; ++i)
    {
        fixture.session->update(kSessionStep);
    }
    CHECK(flatDistance(positionOf(world, agent), destination) < 0.3f);
    CHECK(std::abs(fixture.session->physics().characterVelocity(agent).x) < 0.05f);
}

TEST_CASE("the Navigation step gives agents states before start and moves them only while started")
{
    SessionFixture fixture;
    World& world = fixture.session->world();
    buildLevel(world);
    const Entity agent = addCharacter(world, Vec3(-6.0f, 0.0f, -6.0f), Vec3(-6.0f, 0.0f, -18.0f));
    for (u32 i = 0; i < 10; ++i)
    {
        fixture.session->update(kSessionStep);
    }
    REQUIRE(world.has<NavAgentState>(agent));
    CHECK(world.get<NavAgentState>(agent).speed == 0.0f);
    fixture.session->start();
    for (u32 i = 0; i < 60; ++i)
    {
        fixture.session->update(kSessionStep);
    }
    CHECK(positionOf(world, agent).z < -7.0f);
    CHECK(&fixture.session->navigation().world() == &world);
}
