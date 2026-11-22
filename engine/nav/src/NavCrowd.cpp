#include "NavCrowd.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <DetourAlloc.h>
#include <DetourLocalBoundary.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourObstacleAvoidance.h>
#include <DetourPathCorridor.h>
#include <DetourProximityGrid.h>
#include <DetourStatus.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <vector>

namespace illusion::detail
{
namespace
{

constexpr u32 kNone = 0xFFFFFFFFu;
constexpr u32 kMaxCorners = 4;             // steering corners of an agent (as dtCrowd)
constexpr u32 kMaxNeighbours = 16;         // NavWorldDesc::maxNeighbours is clamped to this
constexpr u32 kMaxObstacleCircles = 8;     // cylinder NavObstacles an agent avoids
constexpr u32 kMaxObstacleSegments = 24;   // wall segments and box NavObstacle edges an agent avoids
constexpr u32 kMaxGridIds = 64;            // agents read from the proximity grid per agent
constexpr i32 kSearchNodes = 2048;         // A* nodes of a path search
constexpr i32 kSteerNodes = 512;           // nodes of the corridor and boundary queries (as dtCrowd)
constexpr i32 kLookAhead = 10;             // corridor polygons checked every update
constexpr f32 kGridCell = 1.0f;            // cell of the proximity grid in m
constexpr f32 kQueryRangeScale = 12.0f;    // neighbour and wall range in agent radii
constexpr f32 kOptimizeRangeScale = 30.0f; // visibility optimisation range in agent radii
constexpr f32 kTriggerScale = 2.25f;       // distance in agent radii at which a link is entered
constexpr f32 kOffMeshMargin = 1.0f;       // an agent farther than its radius plus this is off the navmesh
constexpr f32 kMinArrival = 0.01f;         // smallest stopping distance in m
constexpr f32 kMinSpeed = 1e-4f;           // slower is standing still, in m/s
constexpr f32 kTurnSpeed = 0.05f;          // slower movement does not turn the agent, in m/s
constexpr f32 kMinLinkSpeed = 0.1f;        // crossing speed of a link of an agent without speed, in m/s
constexpr f32 kDebugLift = 0.02f;
constexpr u32 kDebugSegments = 12;

[[nodiscard]] u32 bitsOf(f32 value) noexcept
{
    return std::bit_cast<u32>(value);
}

[[nodiscard]] bool sameBits(const Vec3& a, const Vec3& b) noexcept
{
    return bitsOf(a.x) == bitsOf(b.x) && bitsOf(a.y) == bitsOf(b.y) && bitsOf(a.z) == bitsOf(b.z);
}

[[nodiscard]] f32 distance2D(const Vec3& a, const Vec3& b) noexcept
{
    const f32 dx = a.x - b.x;
    const f32 dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

[[nodiscard]] f32 length2D(const Vec3& v) noexcept
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}

[[nodiscard]] f32 wrapAngle(f32 angle) noexcept
{
    constexpr f32 kPi = std::numbers::pi_v<f32>;
    return angle - 2.0f * kPi * std::floor((angle + kPi) / (2.0f * kPi));
}

[[nodiscard]] bool finite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct QueryDeleter
{
    void operator()(dtNavMeshQuery* query) const noexcept { dtFreeNavMeshQuery(query); }
    void operator()(dtObstacleAvoidanceQuery* query) const noexcept { dtFreeObstacleAvoidanceQuery(query); }
    void operator()(dtProximityGrid* grid) const noexcept { dtFreeProximityGrid(grid); }
};

using QueryPtr = std::unique_ptr<dtNavMeshQuery, QueryDeleter>;
using AvoidancePtr = std::unique_ptr<dtObstacleAvoidanceQuery, QueryDeleter>;
using GridPtr = std::unique_ptr<dtProximityGrid, QueryDeleter>;

enum class Target : u8
{
    None,      // no destination
    Requested, // waits for a search to start
    Searching, // a search runs
    Valid,     // follows its corridor
    Arrived,   // at the end of its corridor
    Failed,    // no path
    OffMesh,   // waits to come back to the navmesh
};

struct LinkInfo
{
    Entity entity;
    NavLinkKind kind = NavLinkKind::Jump;
    f32 height = 1.0f;
    f32 seconds = 0.0f;
};

// What the other agents read of an agent: its state at the start of the update.
struct Snapshot
{
    Vec3 position{0.0f};
    Vec3 velocity{0.0f};
    Vec3 desired{0.0f};
    f32 radius = 0.0f;
    f32 height = 0.0f;
    u8 priority = 0;
    bool active = false;
};

struct Obstacle
{
    Vec3 center{0.0f};
    Vec3 halfExtents{0.0f};
    f32 yaw = 0.0f;
    bool cylinder = false;
};

struct Slot
{
    Entity entity;
    bool used = false;
    bool fresh = false;    // added in this update
    bool relocate = false; // find its polygon again
    bool replan = false;   // search again once placed
    bool warp = false;     // warp() moved its Transform
    bool inactive = false;
    bool onLink = false;
    bool hasDestination = false;
    bool partial = false;
    bool arrivedEvent = false;
    bool failedEvent = false;
    bool enteredEvent = false;
    bool exitedEvent = false;
    bool writePosition = false;
    u32 stamp = 0;
    u32 request = 0; // grows with every search requested
    u32 search = kNone;
    Target target = Target::None;
    NavAgentStatus status = NavAgentStatus::Idle;

    // NavAgent values of this update.
    u32 agentType = 0;
    u32 meshType = kNone; // agent type of the navmesh the corridor refers to
    u32 meshGeneration = 0;
    f32 radius = 0.3f;
    f32 height = 1.8f;
    f32 maxSpeed = 3.5f;
    f32 maxAcceleration = 8.0f;
    f32 turnSpeed = 6.0f;
    f32 stoppingDistance = 0.1f;
    f32 separationWeight = 2.0f;
    NavAvoidance avoidance = NavAvoidance::Medium;
    NavDrive drive = NavDrive::Character;
    u8 priority = 50;
    bool faceMovement = true;
    dtQueryFilter filter;

    Vec3 destination{0.0f};
    Vec3 goal{0.0f}; // where it arrived
    Vec3 position{0.0f};
    Vec3 written{0.0f}; // the Transform position after the last update
    Vec3 velocity{0.0f};
    Vec3 desired{0.0f};
    Vec3 planned{0.0f}; // velocity after avoidance
    f32 desiredSpeed = 0.0f;
    f32 yaw = 0.0f; // of the Entity's +z axis at the start of the update
    f32 turn = 0.0f;
    bool hasYaw = false;
    f32 remaining = 0.0f;

    dtPathCorridor corridor;
    dtLocalBoundary boundary;
    bool corridorReady = false;
    std::array<Vec3, kMaxCorners> corners{};
    std::array<u8, kMaxCorners> cornerFlags{};
    std::array<dtPolyRef, kMaxCorners> cornerPolys{};
    u32 cornerCount = 0;
    std::array<u32, kMaxNeighbours> neighbours{};
    u32 neighbourCount = 0;

    // Link traversal.
    Vec3 linkInit{0.0f};
    Vec3 linkStart{0.0f};
    Vec3 linkEnd{0.0f};
    f32 linkTime = 0.0f;
    f32 linkApproach = 0.0f;
    f32 linkCrossing = 0.0f;
    f32 linkHeight = 0.0f;
    NavLinkKind linkKind = NavLinkKind::Jump;
    Entity link;
};

enum class SearchState : u8
{
    Free,
    Start,
    Running,
    Done,
};

enum class SearchResult : u8
{
    Found,
    Partial,
    NoStart,
    NoEnd,
    NoPath,
};

struct Search
{
    QueryPtr query;
    dtQueryFilter filter;
    std::unique_ptr<dtPolyRef[]> path;
    i32 count = 0;
    u32 slot = kNone;
    u32 request = 0;
    u32 agentType = 0;
    u32 generation = 0;
    const dtNavMesh* mesh = nullptr;
    Vec3 from{0.0f};
    Vec3 to{0.0f};
    Vec3 extents{0.0f};
    Vec3 startPoint{0.0f};
    Vec3 endPoint{0.0f};
    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    SearchState state = SearchState::Free;
    SearchResult result = SearchResult::NoPath;
};

struct Worker
{
    std::array<QueryPtr, kMaxNavAgentTypes> queries;
    std::array<const dtNavMesh*, kMaxNavAgentTypes> meshes{};
    std::array<u32, kMaxNavAgentTypes> generations{};
    AvoidancePtr avoidance;
};

[[nodiscard]] dtObstacleAvoidanceParams avoidanceParams(NavAvoidance quality) noexcept
{
    dtObstacleAvoidanceParams params{};
    params.velBias = 0.5f;
    params.weightDesVel = 2.0f;
    params.weightCurVel = 0.75f;
    params.weightSide = 0.75f;
    params.weightToi = 2.5f;
    params.horizTime = 2.5f;
    params.gridSize = 33;
    params.adaptiveDivs = quality == NavAvoidance::High ? 7 : 5;
    params.adaptiveRings = 2;
    params.adaptiveDepth = quality == NavAvoidance::Low ? 1 : (quality == NavAvoidance::Medium ? 2 : 3);
    return params;
}

// The direction to steer in: towards the first corner, bending towards the second (dtCrowd's anticipated
// turns).
[[nodiscard]] Vec3 steerDirection(const Slot& slot, const Vec3& position) noexcept
{
    if (slot.cornerCount == 0)
    {
        return Vec3(0.0f);
    }
    const Vec3& p0 = slot.corners[0];
    const Vec3& p1 = slot.corners[std::min(1u, slot.cornerCount - 1)];
    Vec3 dir0 = p0 - position;
    Vec3 dir1 = p1 - position;
    dir0.y = 0.0f;
    dir1.y = 0.0f;
    const f32 len0 = glm::length(dir0);
    const f32 len1 = glm::length(dir1);
    if (len1 > 0.001f)
    {
        dir1 /= len1;
    }
    Vec3 dir(dir0.x - dir1.x * len0 * 0.5f, 0.0f, dir0.z - dir1.z * len0 * 0.5f);
    const f32 length = glm::length(dir);
    return length > 1e-6f ? dir / length : Vec3(0.0f);
}

} // namespace

struct NavCrowd::Impl
{
    World* world = nullptr;
    NavWorldDesc desc;
    u32 capacity = 0;
    u32 neighbourLimit = 0;
    u32 stamp = 0;
    std::unique_ptr<Slot[]> slots;
    std::unique_ptr<Snapshot[]> snapshots;
    std::vector<u32> slotOf; // by Entity index
    std::vector<u32> order;  // used slots by Entity index
    std::vector<Entity> added;
    std::vector<Entity> removed;
    bool orderDirty = false;
    bool capacityLogged = false;
    std::unique_ptr<Search[]> searches;
    u32 searchCount = 0;
    std::vector<u32> running; // searches with work this update
    std::vector<Worker> workers;
    GridPtr grid;
    std::vector<Obstacle> obstacles;
    std::array<std::vector<LinkInfo>, kMaxNavAgentTypes> links;
    std::array<const EntityGuid*, kMaxNavAgentTypes> linkSources{};
    std::array<usize, kMaxNavAgentTypes> linkCounts{};
    u64 linkVersion = 0;
    u64 linkStructure = 0;
    std::vector<NavEvent> events;
    std::array<dtObstacleAvoidanceParams, 4> avoidance{};

    // --- Helpers --------------------------------------------------------------------------------------

    [[nodiscard]] u32 workerSlot() const noexcept
    {
        const auto last = static_cast<u32>(workers.size() - 1);
        const i32 index = JobSystem::currentWorkerIndex();
        return index < 0 || static_cast<u32>(index) >= last ? last : static_cast<u32>(index);
    }

    [[nodiscard]] static Vec3 extentsOf(const Slot& slot) noexcept
    {
        const f32 reach = std::max(slot.radius, 0.0f) + kOffMeshMargin;
        return Vec3(reach, std::max(slot.height, 0.5f), reach);
    }

    void cancelSearch(Slot& slot) noexcept
    {
        if (slot.search != kNone)
        {
            searches[slot.search].state = SearchState::Free;
            searches[slot.search].slot = kNone;
            slot.search = kNone;
        }
    }

    void requestPath(Slot& slot) noexcept
    {
        cancelSearch(slot);
        ++slot.request;
        slot.target = Target::Requested;
        slot.status = NavAgentStatus::Pending;
        slot.partial = false;
        slot.cornerCount = 0;
        slot.remaining = 0.0f;
    }

    void clearPath(Slot& slot) noexcept
    {
        cancelSearch(slot);
        ++slot.request;
        slot.target = Target::None;
        slot.status = NavAgentStatus::Idle;
        slot.partial = false;
        slot.cornerCount = 0;
        slot.remaining = 0.0f;
    }

    void release(u32 index)
    {
        Slot& slot = slots[index];
        cancelSearch(slot);
        if (slot.entity.index < slotOf.size() && slotOf[slot.entity.index] == index)
        {
            slotOf[slot.entity.index] = kNone;
        }
        slot.used = false;
        slot.onLink = false;
        slot.entity = kNullEntity;
        orderDirty = true;
    }

    [[nodiscard]] u32 slotIndex(Entity entity) const noexcept
    {
        if (entity.index >= slotOf.size())
        {
            return kNone;
        }
        const u32 index = slotOf[entity.index];
        return index != kNone && slots[index].used && slots[index].entity == entity ? index : kNone;
    }

    // --- Phase 1: slots, positions, destinations ------------------------------------------------------

    void readAgent(Slot& slot, const NavAgent& agent, const Transform& transform)
    {
        const bool typeChanged = slot.agentType != agent.agentType;
        slot.agentType = agent.agentType;
        slot.radius = std::isfinite(agent.radius) ? std::max(agent.radius, 0.0f) : 0.0f;
        slot.height = std::isfinite(agent.height) ? std::max(agent.height, 0.0f) : 0.0f;
        slot.maxSpeed = std::isfinite(agent.maxSpeed) ? std::max(agent.maxSpeed, 0.0f) : 0.0f;
        slot.maxAcceleration =
            std::isfinite(agent.maxAcceleration) ? std::max(agent.maxAcceleration, 0.0f) : 0.0f;
        slot.turnSpeed = std::isfinite(agent.turnSpeed) ? std::max(agent.turnSpeed, 0.0f) : 0.0f;
        slot.stoppingDistance = std::isfinite(agent.stoppingDistance)
                                    ? std::max(agent.stoppingDistance, kMinArrival)
                                    : kMinArrival;
        slot.separationWeight =
            std::isfinite(agent.separationWeight) ? std::max(agent.separationWeight, 0.0f) : 0.0f;
        slot.avoidance = agent.avoidance;
        slot.priority = agent.priority;
        slot.drive = agent.drive;
        slot.faceMovement = agent.faceMovement;
        slot.filter.setIncludeFlags(static_cast<u16>(agent.areaMask & 0xFFFFu));
        slot.filter.setExcludeFlags(0);

        const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
        slot.hasYaw = forward.x * forward.x + forward.z * forward.z > 1e-8f;
        slot.yaw = slot.hasYaw ? std::atan2(forward.x, forward.z) : 0.0f;
        slot.turn = 0.0f;

        const Vec3 position = finite(transform.position) ? transform.position : slot.position;
        if (slot.fresh)
        {
            slot.position = position;
            slot.written = position;
            slot.relocate = true;
        }
        else if (std::exchange(slot.warp, false))
        {
            slot.onLink = false;
            slot.position = position;
            slot.relocate = true;
            slot.replan = true;
        }
        else if (!slot.onLink)
        {
            const bool moved = !sameBits(position, slot.written);
            if (slot.drive != NavDrive::Transform)
            {
                slot.position = position;
            }
            else if (moved)
            {
                slot.position = position;
                slot.relocate = true;
                slot.replan = true;
            }
        }
        if (typeChanged)
        {
            slot.relocate = true;
            slot.replan = true;
        }

        const bool has = agent.destination.has_value() && finite(*agent.destination);
        const bool changed =
            has != slot.hasDestination || (has && !sameBits(*agent.destination, slot.destination));
        if (changed || slot.fresh)
        {
            slot.hasDestination = has;
            slot.destination = has ? *agent.destination : Vec3(0.0f);
            if (has)
            {
                requestPath(slot);
            }
            else
            {
                clearPath(slot);
            }
        }
    }

    void syncSlots()
    {
        ++stamp;
        added.clear();
        removed.clear();
        world->query<const NavAgent, const Transform>().each(
            [&](Entity entity, const NavAgent&, const Transform&) {
                const u32 index = slotIndex(entity);
                if (index == kNone)
                {
                    added.push_back(entity);
                    return;
                }
                slots[index].stamp = stamp;
            });
        for (u32 i = 0; i < capacity; ++i)
        {
            Slot& slot = slots[i];
            if (slot.used && slot.stamp != stamp)
            {
                if (world->isAlive(slot.entity) && world->has<NavAgentState>(slot.entity))
                {
                    removed.push_back(slot.entity);
                }
                release(i);
            }
        }
        std::ranges::sort(added, {}, &Entity::index);
        u32 next = 0;
        for (const Entity entity : added)
        {
            while (next < capacity && slots[next].used)
            {
                ++next;
            }
            if (next == capacity)
            {
                if (!std::exchange(capacityLogged, true))
                {
                    ILL_LOG_WARN("Navigation", "more than {} NavAgents: the others do not move", capacity);
                }
                break;
            }
            Slot& slot = slots[next];
            if (!slot.corridorReady)
            {
                slot.corridorReady = slot.corridor.init(static_cast<i32>(std::max(desc.maxPathPolygons, 8u)));
                ILL_CHECK(slot.corridorReady, "out of memory for a navigation corridor");
            }
            slot.entity = entity;
            slot.used = true;
            slot.fresh = true;
            slot.stamp = stamp;
            slot.onLink = false;
            slot.hasDestination = false;
            slot.target = Target::None;
            slot.status = NavAgentStatus::Idle;
            slot.velocity = Vec3(0.0f);
            slot.desired = Vec3(0.0f);
            slot.planned = Vec3(0.0f);
            slot.meshType = kNone;
            slot.cornerCount = 0;
            slot.neighbourCount = 0;
            slot.corridor.reset(0, glm::value_ptr(slot.position));
            slot.boundary.reset();
            if (slotOf.size() <= entity.index)
            {
                slotOf.resize(entity.index + 1, kNone);
            }
            slotOf[entity.index] = next;
            orderDirty = true;
        }
        for (const Entity entity : removed)
        {
            static_cast<void>(world->remove<NavAgentState>(entity));
        }
        for (const Entity entity : added)
        {
            const u32 index = slotIndex(entity);
            if (index != kNone && !world->has<NavAgentState>(entity))
            {
                world->set(entity, NavAgentState{.slot = index,
                                                 .status = NavAgentStatus::Idle,
                                                 .velocity = Vec3(0.0f),
                                                 .speed = 0.0f,
                                                 .remainingDistance = 0.0f,
                                                 .link = kNullEntity});
            }
        }
        if (orderDirty)
        {
            order.clear();
            for (u32 i = 0; i < capacity; ++i)
            {
                if (slots[i].used)
                {
                    order.push_back(i);
                }
            }
            std::ranges::sort(order,
                              [this](u32 a, u32 b) { return slots[a].entity.index < slots[b].entity.index; });
            orderDirty = false;
        }
        for (const u32 index : order)
        {
            Slot& slot = slots[index];
            const NavAgent& agent = world->get<NavAgent>(slot.entity);
            const Transform& transform = world->get<Transform>(slot.entity);
            slot.inactive = world->has<Inactive>(slot.entity);
            readAgent(slot, agent, transform);
            slot.fresh = false;
        }
    }

    void prepareSurfaces(std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces, JobSystem* jobs)
    {
        const u32 count = jobs != nullptr ? jobs->workerCount() + 1 : 1;
        if (workers.size() < count)
        {
            workers.resize(count);
        }
        for (Worker& worker : workers)
        {
            if (!worker.avoidance)
            {
                worker.avoidance.reset(dtAllocObstacleAvoidanceQuery());
                ILL_CHECK(worker.avoidance &&
                              worker.avoidance->init(static_cast<i32>(kMaxNeighbours + kMaxObstacleCircles),
                                                     static_cast<i32>(kMaxObstacleSegments)),
                          "out of memory for navigation avoidance");
            }
            for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
            {
                const NavCrowdSurface& surface = surfaces[type];
                if (surface.mesh == nullptr ||
                    (worker.meshes[type] == surface.mesh && worker.generations[type] == surface.generation))
                {
                    continue;
                }
                if (!worker.queries[type])
                {
                    worker.queries[type].reset(dtAllocNavMeshQuery());
                    ILL_CHECK(worker.queries[type] != nullptr, "out of memory for a navigation query");
                }
                ILL_CHECK(dtStatusSucceed(worker.queries[type]->init(surface.mesh, kSteerNodes)),
                          "out of memory for a navigation query");
                worker.meshes[type] = surface.mesh;
                worker.generations[type] = surface.generation;
            }
        }
        for (const u32 index : order)
        {
            Slot& slot = slots[index];
            const NavCrowdSurface* surface =
                slot.agentType < kMaxNavAgentTypes ? &surfaces[slot.agentType] : nullptr;
            for (u32 area = 0; area < kMaxNavAreas; ++area)
            {
                const f32 cost = surface != nullptr ? surface->areaCosts[area] : 1.0f;
                slot.filter.setAreaCost(static_cast<i32>(area),
                                        std::isfinite(cost) ? std::max(cost, 1.0f) : 1.0f);
            }
            const bool meshChanged = surface == nullptr || surface->mesh == nullptr ||
                                     slot.meshType != slot.agentType ||
                                     slot.meshGeneration != surface->generation;
            if (meshChanged && slot.meshType != kNone)
            {
                // The corridor refers to polygons of another navmesh.
                slot.onLink = false;
                slot.relocate = true;
                slot.replan = slot.hasDestination;
                slot.meshType = kNone;
                if (slot.target == Target::Searching || slot.target == Target::Valid ||
                    slot.target == Target::Arrived)
                {
                    requestPath(slot);
                }
            }
            if (surface != nullptr && surface->mesh != nullptr && slot.meshType == kNone)
            {
                slot.meshType = slot.agentType;
                slot.meshGeneration = surface->generation;
            }
        }
        for (u32 i = 0; i < searchCount; ++i)
        {
            Search& search = searches[i];
            if (search.state == SearchState::Free)
            {
                continue;
            }
            const NavCrowdSurface& surface = surfaces[search.agentType];
            if (surface.mesh != search.mesh || surface.generation != search.generation)
            {
                Slot& slot = slots[search.slot];
                search.state = SearchState::Free;
                search.slot = kNone;
                slot.search = kNone;
                requestPath(slot);
            }
        }
    }

    void syncLinks(std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces)
    {
        const u64 version = world->componentVersion<NavLink>();
        const u64 structure = world->structuralVersion();
        bool same = version == linkVersion && structure == linkStructure;
        for (u32 type = 0; type < kMaxNavAgentTypes && same; ++type)
        {
            same = linkSources[type] == surfaces[type].links.data() &&
                   linkCounts[type] == surfaces[type].links.size();
        }
        if (same)
        {
            return;
        }
        linkVersion = version;
        linkStructure = structure;
        for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
        {
            linkSources[type] = surfaces[type].links.data();
            linkCounts[type] = surfaces[type].links.size();
            links[type].assign(surfaces[type].links.size(), LinkInfo{});
        }
        world->query<const NavLink, const EntityGuid>().each(
            [&](Entity entity, const NavLink& link, const EntityGuid& guid) {
                for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
                {
                    const std::span<const EntityGuid> guids = surfaces[type].links;
                    for (usize i = 0; i < guids.size(); ++i)
                    {
                        if (guids[i] == guid)
                        {
                            links[type][i] = LinkInfo{
                                .entity = entity,
                                .kind = link.kind,
                                .height = std::isfinite(link.height) ? std::max(link.height, 0.0f) : 0.0f,
                                .seconds = std::isfinite(link.seconds) ? std::max(link.seconds, 0.0f) : 0.0f};
                        }
                    }
                }
            });
    }

    // --- Phase 2: path searches -----------------------------------------------------------------------

    void startSearches(std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces)
    {
        u32 started = 0;
        for (u32 i = 0; i < capacity && started < desc.maxPathRequestsPerUpdate; ++i)
        {
            Slot& slot = slots[i];
            if (!slot.used || slot.inactive || slot.onLink || slot.target != Target::Requested ||
                slot.agentType >= kMaxNavAgentTypes || surfaces[slot.agentType].mesh == nullptr)
            {
                continue;
            }
            u32 free = 0;
            while (free < searchCount && searches[free].state != SearchState::Free)
            {
                ++free;
            }
            if (free == searchCount)
            {
                break;
            }
            cancelSearch(slot);
            Search& search = searches[free];
            const NavCrowdSurface& surface = surfaces[slot.agentType];
            if (!search.query)
            {
                search.query.reset(dtAllocNavMeshQuery());
                ILL_CHECK(search.query && dtStatusSucceed(search.query->init(surface.mesh, kSearchNodes)),
                          "out of memory for a navigation path search");
                search.path = std::make_unique<dtPolyRef[]>(std::max(desc.maxPathPolygons, 8u));
            }
            search.filter = slot.filter;
            search.slot = i;
            search.request = slot.request;
            search.agentType = slot.agentType;
            search.generation = surface.generation;
            search.mesh = surface.mesh;
            search.from = slot.position;
            search.to = slot.destination;
            search.extents = extentsOf(slot);
            search.count = 0;
            search.state = SearchState::Start;
            slot.search = free;
            slot.target = Target::Searching;
            ++started;
        }
    }

    void runSearch(Search& search) const
    {
        dtNavMeshQuery& query = *search.query;
        if (search.state == SearchState::Start)
        {
            if (dtStatusFailed(query.init(search.mesh, kSearchNodes)))
            {
                search.result = SearchResult::NoPath;
                search.state = SearchState::Done;
                return;
            }
            search.startRef = 0;
            search.endRef = 0;
            query.findNearestPoly(glm::value_ptr(search.from), glm::value_ptr(search.extents), &search.filter,
                                  &search.startRef, glm::value_ptr(search.startPoint));
            if (search.startRef == 0)
            {
                search.result = SearchResult::NoStart;
                search.state = SearchState::Done;
                return;
            }
            const Vec3 reach(2.0f, 4.0f, 2.0f);
            query.findNearestPoly(glm::value_ptr(search.to), glm::value_ptr(reach), &search.filter,
                                  &search.endRef, glm::value_ptr(search.endPoint));
            if (search.endRef == 0)
            {
                search.result = SearchResult::NoEnd;
                search.state = SearchState::Done;
                return;
            }
            if (dtStatusFailed(query.initSlicedFindPath(search.startRef, search.endRef,
                                                        glm::value_ptr(search.startPoint),
                                                        glm::value_ptr(search.endPoint), &search.filter)))
            {
                search.result = SearchResult::NoPath;
                search.state = SearchState::Done;
                return;
            }
            search.state = SearchState::Running;
        }
        const dtStatus status =
            query.updateSlicedFindPath(static_cast<i32>(std::max(desc.maxPathIterations, 1u)), nullptr);
        if (dtStatusInProgress(status))
        {
            return;
        }
        search.state = SearchState::Done;
        const i32 maxPath = static_cast<i32>(std::max(desc.maxPathPolygons, 8u));
        if (dtStatusFailed(status) ||
            dtStatusFailed(query.finalizeSlicedFindPath(search.path.get(), &search.count, maxPath)) ||
            search.count == 0)
        {
            search.result = SearchResult::NoPath;
            return;
        }
        const dtPolyRef last = search.path[static_cast<usize>(search.count - 1)];
        if (last == search.endRef)
        {
            search.result = SearchResult::Found;
            return;
        }
        Vec3 clamped = search.endPoint;
        if (dtStatusFailed(query.closestPointOnPoly(last, glm::value_ptr(search.endPoint),
                                                    glm::value_ptr(clamped), nullptr)))
        {
            search.result = SearchResult::NoPath;
            return;
        }
        // As NavWorld::findPath(): a partial path that gets no nearer the destination is no path.
        if (glm::distance(clamped, search.endPoint) >=
            glm::distance(search.startPoint, search.endPoint) - 1e-3f)
        {
            search.result = SearchResult::NoPath;
            return;
        }
        search.endPoint = clamped;
        search.result = SearchResult::Partial;
    }

    void applySearch(u32 index, NavStatistics& statistics)
    {
        Search& search = searches[index];
        Slot& slot = slots[search.slot];
        search.state = SearchState::Free;
        search.slot = kNone;
        if (slot.search == index)
        {
            slot.search = kNone;
        }
        ++statistics.pathSearches;
        if (search.request != slot.request || slot.target != Target::Searching)
        {
            return;
        }
        switch (search.result)
        {
        case SearchResult::NoStart:
            slot.target = Target::OffMesh;
            slot.status = NavAgentStatus::OffMesh;
            slot.relocate = true;
            slot.replan = true;
            return;
        case SearchResult::NoEnd:
        case SearchResult::NoPath:
            slot.target = Target::Failed;
            slot.status = NavAgentStatus::Failed;
            slot.failedEvent = true;
            slot.cornerCount = 0;
            return;
        case SearchResult::Found:
        case SearchResult::Partial:
            break;
        }
        slot.corridor.reset(search.startRef, glm::value_ptr(search.startPoint));
        slot.corridor.setCorridor(glm::value_ptr(search.endPoint), search.path.get(), search.count);
        slot.boundary.reset();
        if (slot.drive == NavDrive::Transform)
        {
            slot.position = search.startPoint;
            slot.writePosition = true;
        }
        slot.relocate = false;
        slot.replan = false;
        slot.partial = search.result == SearchResult::Partial;
        slot.target = Target::Valid;
        slot.status = slot.partial ? NavAgentStatus::Partial : NavAgentStatus::Moving;
    }

    void runSearches(JobSystem* jobs, NavStatistics& statistics)
    {
        running.clear();
        for (u32 i = 0; i < searchCount; ++i)
        {
            if (searches[i].state == SearchState::Start || searches[i].state == SearchState::Running)
            {
                running.push_back(i);
            }
        }
        if (!running.empty())
        {
            ILL_PROFILE_SCOPE_N("NavCrowd::searches");
            const auto work = [this](u32 begin, u32 end) {
                for (u32 i = begin; i < end; ++i)
                {
                    runSearch(searches[running[i]]);
                }
            };
            if (jobs != nullptr)
            {
                jobs->parallelFor(static_cast<u32>(running.size()), 1, work);
            }
            else
            {
                work(0, static_cast<u32>(running.size()));
            }
        }
        for (u32 i = 0; i < searchCount; ++i)
        {
            if (searches[i].state == SearchState::Done)
            {
                applySearch(i, statistics);
            }
        }
    }

    // --- Phase 3: neighbour grid and obstacles --------------------------------------------------------

    void buildGrid()
    {
        grid->clear();
        for (const u32 index : order)
        {
            const Slot& slot = slots[index];
            Snapshot& snapshot = snapshots[index];
            snapshot = Snapshot{.position = slot.position,
                                .velocity = slot.velocity,
                                .desired = slot.desired,
                                .radius = slot.radius,
                                .height = slot.height,
                                .priority = slot.priority,
                                .active = !slot.inactive && !slot.onLink && slot.meshType != kNone};
            if (snapshot.active)
            {
                const f32 r = slot.radius;
                grid->addItem(static_cast<u16>(index), slot.position.x - r, slot.position.z - r,
                              slot.position.x + r, slot.position.z + r);
            }
        }
        obstacles.clear();
        world->query<const NavObstacle, const Transform>().without<Inactive>().each(
            [&](const NavObstacle& obstacle, const Transform& transform) {
                const bool cylinder = obstacle.shape == NavObstacleShape::Cylinder;
                const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
                obstacles.push_back(Obstacle{
                    .center = transform.position,
                    .halfExtents = cylinder ? Vec3(obstacle.radius, obstacle.height * 0.5f, obstacle.radius)
                                            : obstacle.size * 0.5f,
                    .yaw = cylinder ? 0.0f : std::atan2(forward.x, forward.z),
                    .cylinder = cylinder});
            });
    }

    // --- Phase 4: corridor, corners, neighbours, steering, avoidance ----------------------------------

    // Finds the agent's polygon again; false when it is off the navmesh.
    bool place(Slot& slot, dtNavMeshQuery& query)
    {
        const Vec3 extents = extentsOf(slot);
        dtPolyRef ref = 0;
        Vec3 nearest = slot.position;
        query.findNearestPoly(glm::value_ptr(slot.position), glm::value_ptr(extents), &slot.filter, &ref,
                              glm::value_ptr(nearest));
        if (ref == 0)
        {
            slot.corridor.reset(0, glm::value_ptr(slot.position));
            slot.boundary.reset();
            if (slot.hasDestination)
            {
                // A running search is dropped when it finishes: its request no longer matches.
                ++slot.request;
                slot.target = Target::OffMesh;
                slot.status = NavAgentStatus::OffMesh;
                slot.replan = true;
            }
            slot.relocate = true;
            return false;
        }
        slot.corridor.reset(ref, glm::value_ptr(nearest));
        slot.boundary.reset();
        if (slot.drive == NavDrive::Transform)
        {
            slot.position = nearest;
            slot.writePosition = true;
        }
        slot.relocate = false;
        if (slot.hasDestination && (slot.replan || slot.target == Target::OffMesh ||
                                    slot.target == Target::Valid || slot.target == Target::Arrived))
        {
            // Searches start on the World's thread; a running one is cancelled there.
            if (slot.target != Target::Requested && slot.target != Target::Searching)
            {
                slot.target = Target::Requested;
                slot.status = NavAgentStatus::Pending;
                ++slot.request;
            }
        }
        slot.replan = false;
        return true;
    }

    void findNeighbours(Slot& slot, u32 self) const
    {
        slot.neighbourCount = 0;
        const f32 range = slot.radius * kQueryRangeScale;
        std::array<u16, kMaxGridIds> ids{};
        const i32 found =
            grid->queryItems(slot.position.x - range, slot.position.z - range, slot.position.x + range,
                             slot.position.z + range, ids.data(), static_cast<i32>(ids.size()));
        std::array<f32, kMaxNeighbours> distances{};
        for (i32 n = 0; n < found; ++n)
        {
            const u32 other = ids[static_cast<usize>(n)];
            if (other == self)
            {
                continue;
            }
            const Snapshot& snapshot = snapshots[other];
            if (!snapshot.active || snapshot.priority > slot.priority ||
                std::abs(snapshot.position.y - slot.position.y) >= (slot.height + snapshot.height) * 0.5f)
            {
                continue;
            }
            const f32 dx = slot.position.x - snapshot.position.x;
            const f32 dz = slot.position.z - snapshot.position.z;
            const f32 distance = dx * dx + dz * dz;
            if (distance > range * range)
            {
                continue;
            }
            // Insertion by distance, then slot: the order does not depend on the grid.
            u32 at = slot.neighbourCount;
            while (at > 0 && (distances[at - 1] > distance ||
                              (distances[at - 1] == distance && slot.neighbours[at - 1] > other)))
            {
                --at;
            }
            if (at >= neighbourLimit)
            {
                continue;
            }
            const u32 last = std::min(slot.neighbourCount, neighbourLimit - 1);
            for (u32 k = last; k > at; --k)
            {
                distances[k] = distances[k - 1];
                slot.neighbours[k] = slot.neighbours[k - 1];
            }
            distances[at] = distance;
            slot.neighbours[at] = other;
            slot.neighbourCount = std::min(slot.neighbourCount + 1, neighbourLimit);
        }
    }

    void addObstacles(const Slot& slot, dtObstacleAvoidanceQuery& query, const Vec3& position) const
    {
        const f32 range = slot.radius * kQueryRangeScale;
        u32 circles = 0;
        u32 segments = static_cast<u32>(query.getObstacleSegmentCount());
        const Vec3 zero(0.0f);
        for (const Obstacle& obstacle : obstacles)
        {
            const f32 reach = obstacle.cylinder ? obstacle.halfExtents.x
                                                : std::sqrt(obstacle.halfExtents.x * obstacle.halfExtents.x +
                                                            obstacle.halfExtents.z * obstacle.halfExtents.z);
            if (distance2D(obstacle.center, position) - reach > range ||
                std::abs(obstacle.center.y - position.y) > obstacle.halfExtents.y + slot.height)
            {
                continue;
            }
            if (obstacle.cylinder)
            {
                if (circles < kMaxObstacleCircles)
                {
                    query.addCircle(glm::value_ptr(obstacle.center), obstacle.halfExtents.x,
                                    glm::value_ptr(zero), glm::value_ptr(zero));
                    ++circles;
                }
                continue;
            }
            const f32 c = std::cos(obstacle.yaw);
            const f32 s = std::sin(obstacle.yaw);
            const f32 signs[4][2] = {{-1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}};
            std::array<Vec3, 4> corners{};
            for (u32 i = 0; i < 4; ++i)
            {
                const f32 lx = signs[i][0] * obstacle.halfExtents.x;
                const f32 lz = signs[i][1] * obstacle.halfExtents.z;
                corners[i] = Vec3(obstacle.center.x + c * lx + s * lz, position.y,
                                  obstacle.center.z - s * lx + c * lz);
            }
            for (u32 i = 0; i < 4 && segments < kMaxObstacleSegments; ++i)
            {
                query.addSegment(glm::value_ptr(corners[i]), glm::value_ptr(corners[(i + 1) % 4]));
                ++segments;
            }
        }
    }

    void steer(u32 index, std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces)
    {
        Slot& slot = slots[index];
        slot.desired = Vec3(0.0f);
        slot.planned = Vec3(0.0f);
        slot.desiredSpeed = 0.0f;
        slot.neighbourCount = 0;
        if (slot.inactive || slot.onLink || slot.agentType >= kMaxNavAgentTypes ||
            surfaces[slot.agentType].mesh == nullptr)
        {
            slot.cornerCount = slot.onLink ? slot.cornerCount : 0;
            return;
        }
        Worker& worker = workers[workerSlot()];
        dtNavMeshQuery& query = *worker.queries[slot.agentType];
        const dtQueryFilter& filter = slot.filter;

        if (slot.relocate || !query.isValidPolyRef(slot.corridor.getFirstPoly(), &filter))
        {
            if (!place(slot, query))
            {
                slot.cornerCount = 0;
                return;
            }
        }
        else if (slot.drive != NavDrive::Transform)
        {
            slot.corridor.movePosition(glm::value_ptr(slot.position), &query, &filter);
            const Vec3 onMesh(slot.corridor.getPos()[0], slot.corridor.getPos()[1],
                              slot.corridor.getPos()[2]);
            if (distance2D(onMesh, slot.position) > slot.radius + kOffMeshMargin ||
                std::abs(onMesh.y - slot.position.y) > std::max(slot.height, 0.5f))
            {
                slot.replan = slot.hasDestination;
                if (!place(slot, query))
                {
                    slot.cornerCount = 0;
                    return;
                }
            }
        }
        const Vec3 position(slot.corridor.getPos()[0], slot.corridor.getPos()[1], slot.corridor.getPos()[2]);

        slot.cornerCount = 0;
        slot.remaining = 0.0f;
        if (slot.target == Target::Valid && (!slot.corridor.isValid(kLookAhead, &query, &filter) ||
                                             !query.isValidPolyRef(slot.corridor.getLastPoly(), &filter)))
        {
            slot.target = Target::Requested;
            slot.status = NavAgentStatus::Pending;
            ++slot.request;
        }
        if (slot.target == Target::Arrived &&
            distance2D(position, slot.goal) > slot.stoppingDistance + std::max(slot.radius, kMinArrival))
        {
            // Pushed away from where it arrived: it walks back.
            slot.target = Target::Requested;
            slot.status = NavAgentStatus::Pending;
            ++slot.request;
        }
        if (slot.target == Target::Valid)
        {
            std::array<f32, kMaxCorners * 3> verts{};
            i32 count =
                slot.corridor.findCorners(verts.data(), slot.cornerFlags.data(), slot.cornerPolys.data(),
                                          static_cast<i32>(kMaxCorners), &query, &filter);
            if (count > 0)
            {
                const u32 next = std::min<u32>(1u, static_cast<u32>(count) - 1);
                slot.corridor.optimizePathVisibility(&verts[next * 3], slot.radius * kOptimizeRangeScale,
                                                     &query, &filter);
            }
            slot.cornerCount = static_cast<u32>(std::max(count, 0));
            for (u32 i = 0; i < slot.cornerCount; ++i)
            {
                slot.corners[i] = Vec3(verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]);
            }
            Vec3 previous = position;
            for (u32 i = 0; i < slot.cornerCount; ++i)
            {
                slot.remaining += glm::distance(previous, slot.corners[i]);
                previous = slot.corners[i];
            }
            const bool endsHere =
                slot.cornerCount == 0 || (slot.cornerFlags[slot.cornerCount - 1] & DT_STRAIGHTPATH_END) != 0;
            if (!endsHere)
            {
                const Vec3 target(slot.corridor.getTarget()[0], slot.corridor.getTarget()[1],
                                  slot.corridor.getTarget()[2]);
                slot.remaining += glm::distance(previous, target);
            }
            const Vec3 end = slot.cornerCount > 0 ? slot.corners[slot.cornerCount - 1] : position;
            const f32 toEnd = distance2D(position, end);
            // The end of a partial path lies on the navmesh border, which avoidance keeps it a little from.
            const f32 arrival =
                slot.partial ? std::max(slot.stoppingDistance, slot.radius) : slot.stoppingDistance;
            if (endsHere && toEnd <= arrival && std::abs(end.y - position.y) < std::max(slot.height, 0.5f))
            {
                slot.goal = end;
                slot.target = Target::Arrived;
                slot.status = slot.partial ? NavAgentStatus::Partial : NavAgentStatus::Arrived;
                slot.arrivedEvent = !slot.partial;
                slot.remaining = 0.0f;
                slot.cornerCount = 0;
            }
            else if (slot.cornerCount > 0 &&
                     (slot.cornerFlags[slot.cornerCount - 1] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0 &&
                     toEnd < slot.radius * kTriggerScale && enterLink(slot, query, position, surfaces))
            {
                return;
            }
            else
            {
                const Vec3 direction = steerDirection(slot, position);
                f32 speed = slot.maxSpeed;
                if (endsHere)
                {
                    // Slow enough to stop within half the stopping distance past the point of arrival.
                    const f32 braking = std::max(toEnd - slot.stoppingDistance * 0.5f, 0.0f);
                    speed = std::min(speed, std::sqrt(2.0f * slot.maxAcceleration * braking));
                }
                slot.desiredSpeed = slot.maxSpeed;
                slot.desired = direction * speed;
            }
        }

        findNeighbours(slot, index);
        if (slot.target == Target::Valid && slot.separationWeight > 0.0f && slot.neighbourCount > 0)
        {
            const f32 range = slot.radius * kQueryRangeScale;
            Vec3 push(0.0f);
            f32 weight = 0.0f;
            for (u32 n = 0; n < slot.neighbourCount; ++n)
            {
                const Snapshot& other = snapshots[slot.neighbours[n]];
                Vec3 diff = position - other.position;
                diff.y = 0.0f;
                const f32 distanceSquared = glm::dot(diff, diff);
                if (distanceSquared < 1e-5f || distanceSquared > range * range)
                {
                    continue;
                }
                const f32 distance = std::sqrt(distanceSquared);
                const f32 w = slot.separationWeight * (1.0f - distanceSquared / (range * range));
                push += diff * (w / distance);
                weight += 1.0f;
            }
            if (weight > 1e-4f)
            {
                slot.desired += push / weight;
                const f32 speedSquared = glm::dot(slot.desired, slot.desired);
                const f32 desiredSquared = slot.desiredSpeed * slot.desiredSpeed;
                if (speedSquared > desiredSquared && speedSquared > 0.0f)
                {
                    slot.desired *= desiredSquared / speedSquared;
                }
            }
        }

        slot.planned = slot.desired;
        if (slot.avoidance == NavAvoidance::None ||
            glm::dot(slot.desired, slot.desired) <= kMinSpeed * kMinSpeed)
        {
            return;
        }
        const f32 range = slot.radius * kQueryRangeScale;
        if (distance2D(position, Vec3(slot.boundary.getCenter()[0], slot.boundary.getCenter()[1],
                                      slot.boundary.getCenter()[2])) > range * 0.25f ||
            !slot.boundary.isValid(&query, &filter))
        {
            slot.boundary.update(slot.corridor.getFirstPoly(), glm::value_ptr(position), range, &query,
                                 &filter);
        }
        dtObstacleAvoidanceQuery& avoid = *worker.avoidance;
        avoid.reset();
        for (u32 n = 0; n < slot.neighbourCount; ++n)
        {
            const Snapshot& other = snapshots[slot.neighbours[n]];
            avoid.addCircle(glm::value_ptr(other.position), other.radius, glm::value_ptr(other.velocity),
                            glm::value_ptr(other.desired));
        }
        for (i32 s = 0; s < slot.boundary.getSegmentCount() &&
                        avoid.getObstacleSegmentCount() < static_cast<i32>(kMaxObstacleSegments);
             ++s)
        {
            const f32* segment = slot.boundary.getSegment(s);
            // dtTriArea2D(position, s, s + 3) < 0: the agent is behind the wall.
            const f32 triangle = (segment[3] - position.x) * (segment[2] - position.z) -
                                 (segment[0] - position.x) * (segment[5] - position.z);
            if (triangle < 0.0f)
            {
                continue;
            }
            avoid.addSegment(segment, segment + 3);
        }
        addObstacles(slot, avoid, position);
        const dtObstacleAvoidanceParams& params = avoidance[static_cast<usize>(slot.avoidance)];
        Vec3 planned(0.0f);
        avoid.sampleVelocityAdaptive(glm::value_ptr(position), slot.radius, slot.desiredSpeed,
                                     glm::value_ptr(slot.velocity), glm::value_ptr(slot.desired),
                                     glm::value_ptr(planned), &params, nullptr);
        if (finite(planned))
        {
            slot.planned = planned;
        }
    }

    bool enterLink(Slot& slot, dtNavMeshQuery& query, const Vec3& position,
                   std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces) const
    {
        const dtPolyRef connection = slot.cornerPolys[slot.cornerCount - 1];
        std::array<dtPolyRef, 2> refs{};
        Vec3 start(0.0f);
        Vec3 end(0.0f);
        if (!slot.corridor.moveOverOffmeshConnection(connection, refs.data(), glm::value_ptr(start),
                                                     glm::value_ptr(end), &query))
        {
            return false;
        }
        LinkInfo info;
        const dtOffMeshConnection* link =
            surfaces[slot.agentType].mesh->getOffMeshConnectionByRef(connection);
        const std::vector<LinkInfo>& table = links[slot.agentType];
        if (link != nullptr && link->userId < table.size())
        {
            info = table[link->userId];
        }
        const f32 speed = std::max(slot.maxSpeed, kMinLinkSpeed);
        slot.onLink = true;
        slot.link = info.entity;
        slot.linkKind = info.kind;
        slot.linkHeight = info.height;
        slot.linkInit = position;
        slot.linkStart = start;
        slot.linkEnd = end;
        slot.linkTime = 0.0f;
        slot.linkApproach = glm::distance(position, start) / speed;
        const f32 length = info.kind == NavLinkKind::Ladder
                               ? std::abs(end.y - start.y) + distance2D(start, end)
                               : glm::distance(start, end);
        slot.linkCrossing = info.seconds > 0.0f ? info.seconds : length / speed;
        slot.enteredEvent = true;
        slot.cornerCount = 0;
        slot.velocity = Vec3(0.0f);
        slot.desired = Vec3(0.0f);
        slot.planned = Vec3(0.0f);
        return true;
    }

    // --- Phase 5: integration and link traversal ------------------------------------------------------

    [[nodiscard]] static Vec3 linkPoint(const Slot& slot) noexcept
    {
        if (slot.linkTime < slot.linkApproach && slot.linkApproach > 0.0f)
        {
            return glm::mix(slot.linkInit, slot.linkStart, slot.linkTime / slot.linkApproach);
        }
        const f32 u = slot.linkCrossing > 0.0f
                          ? std::clamp((slot.linkTime - slot.linkApproach) / slot.linkCrossing, 0.0f, 1.0f)
                          : 1.0f;
        const Vec3& a = slot.linkStart;
        const Vec3& b = slot.linkEnd;
        switch (slot.linkKind)
        {
        case NavLinkKind::Jump:
        {
            // y = a.y + rise u + bend u (1 - u), with the bend that puts the apex linkHeight above the higher
            // end: the apex is (rise + bend)² / (4 bend) above a.
            const f32 rise = b.y - a.y;
            const f32 apex = std::max(std::max(a.y, b.y) + slot.linkHeight - a.y, std::max(rise, 0.0f));
            const f32 bend = 2.0f * apex - rise + 2.0f * std::sqrt(std::max(apex * (apex - rise), 0.0f));
            Vec3 point = glm::mix(a, b, u);
            point.y = a.y + rise * u + bend * u * (1.0f - u);
            return point;
        }
        case NavLinkKind::Ladder:
        {
            const f32 rise = std::abs(b.y - a.y);
            const f32 run = distance2D(a, b);
            const f32 total = rise + run;
            if (total <= 0.0f)
            {
                return b;
            }
            // The climb happens at the lower end: up first when climbing, last when going down.
            const bool up = b.y >= a.y;
            const f32 split = (up ? rise : run) / total;
            const Vec3 corner = up ? Vec3(a.x, b.y, a.z) : Vec3(b.x, a.y, b.z);
            if (u < split)
            {
                return glm::mix(a, corner, split > 0.0f ? u / split : 1.0f);
            }
            return glm::mix(corner, b, split < 1.0f ? (u - split) / (1.0f - split) : 1.0f);
        }
        case NavLinkKind::Drop:
        case NavLinkKind::Door:
        case NavLinkKind::Custom:
            break;
        }
        return glm::mix(a, b, u);
    }

    void integrate(u32 index, f32 seconds)
    {
        Slot& slot = slots[index];
        if (slot.inactive)
        {
            return;
        }
        if (slot.onLink)
        {
            slot.linkTime += seconds;
            slot.writePosition = true;
            if (slot.linkTime >= slot.linkApproach + slot.linkCrossing)
            {
                slot.position = slot.linkEnd;
                slot.onLink = false;
                slot.exitedEvent = true;
                slot.boundary.reset();
            }
            else
            {
                slot.position = linkPoint(slot);
            }
            return;
        }
        if (slot.meshType == kNone || slot.corridor.getFirstPoly() == 0)
        {
            slot.velocity = Vec3(0.0f);
            return;
        }
        const f32 maxDelta = slot.maxAcceleration * seconds;
        Vec3 change = slot.planned - slot.velocity;
        change.y = 0.0f;
        const f32 delta = glm::length(change);
        if (delta > maxDelta && delta > 0.0f)
        {
            change *= maxDelta / delta;
        }
        slot.velocity += change;
        if (glm::length(slot.velocity) <= kMinSpeed)
        {
            slot.velocity = Vec3(0.0f);
        }

        const Vec3 heading = slot.drive == NavDrive::RootMotion ? slot.planned : slot.velocity;
        if ((slot.faceMovement || slot.drive == NavDrive::RootMotion) && slot.hasYaw &&
            length2D(heading) > kTurnSpeed)
        {
            const f32 wanted = std::atan2(heading.x, heading.z);
            const f32 limit = slot.turnSpeed * seconds;
            slot.turn = std::clamp(wrapAngle(wanted - slot.yaw), -limit, limit);
        }

        if (slot.drive != NavDrive::Transform)
        {
            return;
        }
        Vec3 next = slot.position + slot.velocity * seconds;
        Vec3 push(0.0f);
        f32 weight = 0.0f;
        for (u32 n = 0; n < slot.neighbourCount; ++n)
        {
            const u32 other = slot.neighbours[n];
            const Snapshot& snapshot = snapshots[other];
            const Vec3 predicted = snapshot.position + snapshot.velocity * seconds;
            Vec3 diff = next - predicted;
            diff.y = 0.0f;
            const f32 reach = slot.radius + snapshot.radius;
            const f32 distanceSquared = glm::dot(diff, diff);
            if (distanceSquared > reach * reach)
            {
                continue;
            }
            const f32 distance = std::sqrt(distanceSquared);
            if (distance < 1e-4f)
            {
                // On top of each other: apart sideways, the two in opposite directions.
                const Vec3 side = index > other ? Vec3(-slot.desired.z, 0.0f, slot.desired.x)
                                                : Vec3(slot.desired.z, 0.0f, -slot.desired.x);
                push += side * 0.01f;
            }
            else
            {
                push += diff * ((reach - distance) * 0.5f / distance);
            }
            weight += 1.0f;
        }
        if (weight > 1e-4f)
        {
            next += push / weight;
        }
        const dtQueryFilter& filter = slot.filter;
        Worker& worker = workers[workerSlot()];
        dtNavMeshQuery& query = *worker.queries[slot.meshType];
        slot.corridor.movePosition(glm::value_ptr(next), &query, &filter);
        slot.position = Vec3(slot.corridor.getPos()[0], slot.corridor.getPos()[1], slot.corridor.getPos()[2]);
        if (slot.target != Target::Valid)
        {
            slot.corridor.reset(slot.corridor.getFirstPoly(), glm::value_ptr(slot.position));
        }
        slot.writePosition = true;
    }

    // --- Phase 6: states, Transforms and events -------------------------------------------------------

    void addEvent(const Slot& slot, NavEventType type, NavStatistics& statistics)
    {
        if (events.size() >= desc.maxEvents)
        {
            ++statistics.droppedEvents;
            return;
        }
        events.push_back(NavEvent{
            .agent = slot.entity,
            .type = type,
            .link = type == NavEventType::LinkEntered || type == NavEventType::LinkExited ? slot.link
                                                                                          : kNullEntity,
            .kind = type == NavEventType::LinkEntered || type == NavEventType::LinkExited ? slot.linkKind
                                                                                          : NavLinkKind::Jump,
            .position = slot.position});
    }

    void publish(bool advance, NavStatistics& statistics)
    {
        bool moved = false;
        for (const u32 index : order)
        {
            Slot& slot = slots[index];
            const bool still = slot.inactive || !advance;
            NavAgentState& state = world->get<NavAgentState>(slot.entity);
            state.slot = index;
            state.status = slot.status;
            state.velocity = still ? Vec3(0.0f) : Vec3(slot.velocity.x, 0.0f, slot.velocity.z);
            state.speed = slot.drive == NavDrive::RootMotion && !still ? length2D(slot.planned)
                                                                       : glm::length(state.velocity);
            state.remainingDistance = slot.remaining;
            state.link = slot.onLink ? slot.link : kNullEntity;
            statistics.moving += !still && (state.speed > 0.01f || slot.onLink) ? 1u : 0u;
            if (!still && (slot.writePosition || slot.turn != 0.0f))
            {
                Transform& transform = world->get<Transform>(slot.entity);
                if (slot.writePosition)
                {
                    transform.position = slot.position;
                }
                if (slot.turn != 0.0f)
                {
                    transform.rotation = glm::normalize(glm::angleAxis(slot.turn, Vec3(0.0f, 1.0f, 0.0f)) *
                                                        transform.rotation);
                }
                const Hierarchy* hierarchy = world->tryGet<Hierarchy>(slot.entity);
                LocalTransform* local = world->tryGet<LocalTransform>(slot.entity);
                if (hierarchy != nullptr && local != nullptr && world->isAlive(hierarchy->parent))
                {
                    const Transform* parent = world->tryGet<Transform>(hierarchy->parent);
                    *local = LocalTransform::from(
                        relativeTo(transform, parent != nullptr ? *parent : Transform{}));
                }
                moved = true;
            }
            slot.writePosition = false;
            slot.written = world->get<Transform>(slot.entity).position;
            if (slot.failedEvent)
            {
                addEvent(slot, NavEventType::PathFailed, statistics);
            }
            if (slot.arrivedEvent)
            {
                addEvent(slot, NavEventType::Arrived, statistics);
            }
            if (slot.enteredEvent)
            {
                addEvent(slot, NavEventType::LinkEntered, statistics);
            }
            if (slot.exitedEvent)
            {
                addEvent(slot, NavEventType::LinkExited, statistics);
                slot.link = kNullEntity;
            }
            slot.failedEvent = false;
            slot.arrivedEvent = false;
            slot.enteredEvent = false;
            slot.exitedEvent = false;
        }
        if (moved)
        {
            world->markChanged<Transform>();
        }
    }

    // --- Update ---------------------------------------------------------------------------------------

    void update(f32 seconds, JobSystem* jobs, bool advance,
                std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces, NavStatistics& statistics)
    {
        events.clear();
        statistics.pathSearches = 0;
        statistics.droppedEvents = 0;
        statistics.moving = 0;
        {
            ILL_PROFILE_SCOPE_N("NavCrowd::sync");
            syncSlots();
            prepareSurfaces(surfaces, jobs);
            syncLinks(surfaces);
        }
        statistics.agents = static_cast<u32>(order.size());
        const f32 step = std::isfinite(seconds) ? std::max(seconds, 0.0f) : 0.0f;
        if (advance && !order.empty())
        {
            startSearches(surfaces);
            runSearches(jobs, statistics);
            {
                ILL_PROFILE_SCOPE_N("NavCrowd::grid");
                buildGrid();
            }
            const auto count = static_cast<u32>(order.size());
            {
                ILL_PROFILE_SCOPE_N("NavCrowd::steer");
                const auto work = [this, surfaces](u32 begin, u32 end) {
                    for (u32 i = begin; i < end; ++i)
                    {
                        steer(order[i], surfaces);
                    }
                };
                if (jobs != nullptr)
                {
                    jobs->parallelFor(count, kBatch, work);
                }
                else
                {
                    work(0, count);
                }
            }
            {
                ILL_PROFILE_SCOPE_N("NavCrowd::integrate");
                const auto work = [this, step](u32 begin, u32 end) {
                    for (u32 i = begin; i < end; ++i)
                    {
                        integrate(order[i], step);
                    }
                };
                if (jobs != nullptr)
                {
                    jobs->parallelFor(count, kBatch, work);
                }
                else
                {
                    work(0, count);
                }
            }
        }
        ILL_PROFILE_SCOPE_N("NavCrowd::publish");
        publish(advance, statistics);
    }

    static constexpr u32 kBatch = 32;
};

NavCrowd::NavCrowd(World& world, const NavWorldDesc& desc)
    : m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    impl.world = &world;
    impl.desc = desc;
    // Grid ids are 16 bits.
    impl.capacity = std::min(std::max(desc.maxAgents, 1u), 65535u);
    impl.neighbourLimit = std::min(desc.maxNeighbours, kMaxNeighbours);
    impl.slots = std::make_unique<Slot[]>(impl.capacity);
    impl.snapshots = std::make_unique<Snapshot[]>(impl.capacity);
    impl.order.reserve(impl.capacity);
    impl.added.reserve(impl.capacity);
    impl.removed.reserve(impl.capacity);
    impl.searchCount = std::max(desc.maxPathRequestsPerUpdate, 1u);
    impl.searches = std::make_unique<Search[]>(impl.searchCount);
    impl.running.reserve(impl.searchCount);
    impl.events.reserve(desc.maxEvents);
    impl.obstacles.reserve(64);
    impl.grid.reset(dtAllocProximityGrid());
    ILL_CHECK(impl.grid && impl.grid->init(static_cast<i32>(impl.capacity * 4), kGridCell),
              "out of memory for the navigation grid");
    for (u32 quality = 0; quality < 4; ++quality)
    {
        impl.avoidance[quality] = avoidanceParams(static_cast<NavAvoidance>(quality));
    }
}

NavCrowd::~NavCrowd() = default;

void NavCrowd::update(f32 seconds, JobSystem* jobs, bool advance,
                      std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces, NavStatistics& statistics)
{
    m_impl->update(seconds, jobs, advance, surfaces, statistics);
}

void NavCrowd::warped(Entity entity) noexcept
{
    const u32 index = m_impl->slotIndex(entity);
    if (index != kNone)
    {
        m_impl->slots[index].warp = true;
    }
}

std::optional<u32> NavCrowd::findSlot(Entity entity) const noexcept
{
    const u32 index = m_impl->slotIndex(entity);
    return index != kNone ? std::optional<u32>(index) : std::nullopt;
}

std::span<const Vec3> NavCrowd::pathCorners(u32 slot) const noexcept
{
    if (slot >= m_impl->capacity || !m_impl->slots[slot].used)
    {
        return {};
    }
    const Slot& s = m_impl->slots[slot];
    return std::span<const Vec3>(s.corners.data(), s.cornerCount);
}

std::span<const NavEvent> NavCrowd::events() const noexcept
{
    return m_impl->events;
}

void NavCrowd::drawDebug(bool agents, bool paths, u32 agentTypeMask, NavDebugSink& sink) const
{
    const Impl& impl = *m_impl;
    const Vec3 lift(0.0f, kDebugLift, 0.0f);
    const Vec4 agentColor(0.2f, 0.7f, 1.0f, 1.0f);
    const Vec4 velocityColor(0.2f, 1.0f, 0.3f, 1.0f);
    const Vec4 pathColor(1.0f, 0.4f, 0.8f, 1.0f);
    for (const u32 index : impl.order)
    {
        const Slot& slot = impl.slots[index];
        if (slot.agentType >= kMaxNavAgentTypes || (agentTypeMask & (1u << slot.agentType)) == 0)
        {
            continue;
        }
        const Vec3 base = slot.position + lift;
        if (agents)
        {
            for (u32 s = 0; s < kDebugSegments; ++s)
            {
                const f32 a =
                    2.0f * std::numbers::pi_v<f32> * static_cast<f32>(s) / static_cast<f32>(kDebugSegments);
                const f32 b = 2.0f * std::numbers::pi_v<f32> * static_cast<f32>(s + 1) /
                              static_cast<f32>(kDebugSegments);
                sink.line(base + Vec3(slot.radius * std::cos(a), 0.0f, slot.radius * std::sin(a)),
                          base + Vec3(slot.radius * std::cos(b), 0.0f, slot.radius * std::sin(b)),
                          agentColor);
            }
            sink.line(base, base + Vec3(0.0f, slot.height, 0.0f), agentColor);
            sink.line(base, base + Vec3(slot.velocity.x, 0.0f, slot.velocity.z), velocityColor);
        }
        if (paths)
        {
            Vec3 previous = base;
            for (u32 c = 0; c < slot.cornerCount; ++c)
            {
                sink.line(previous, slot.corners[c] + lift, pathColor);
                previous = slot.corners[c] + lift;
            }
        }
    }
}

} // namespace illusion::detail
