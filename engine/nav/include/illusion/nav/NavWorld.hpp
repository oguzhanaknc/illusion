#pragma once

/// @file
/// The navigation of a World (docs/adr/0062-navigation-and-ai.md): its NavMeshSurfaces loaded or baked as
/// tiled Detour navmeshes, path and point queries, and the crowd that steers its NavAgents on the Job System.
///
/// @code
/// illusion::NavWorld navigation(world, &streamer);
/// while (running)
/// {
///     streamer.update();
///     ai.update(step, &jobs, &physics, true);   // sets destinations
///     navigation.update(step, &jobs, true);     // before animation and physics
///     // the session gives Character agents NavAgentState::velocity
///     physics.update(step);
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavService.hpp>

#include <memory>
#include <optional>
#include <span>

namespace illusion
{

class AssetStreamer;
class JobSystem;
class World;

/// Creation parameters of a NavWorld.
struct NavWorldDesc
{
    u32 maxAgents = 1024;              ///< Crowd slots, reserved at construction.
    u32 maxPathPolygons = 256;         ///< Polygons of an agent's path corridor.
    u32 maxPathRequestsPerUpdate = 32; ///< Path searches started per update, in slot order.
    u32 maxPathIterations = 512;       ///< A* iterations per search and update (sliced over updates).
    u32 maxNeighbours = 6;             ///< Neighbours an agent avoids.
    u32 maxTileBuildsInFlight = 4;     ///< Tile bakes running in the background at once.
    u32 maxEvents = 1024;              ///< Events kept per update; more are counted and dropped.
};

/// Counters of a NavWorld.
struct NavStatistics
{
    u32 surfaces = 0;     ///< Surfaces with a navmesh.
    u32 tiles = 0;        ///< Tiles with polygons.
    u32 polygons = 0;     ///< Polygons of every navmesh.
    u32 pendingTiles = 0; ///< Tiles waiting to be baked or rebuilt after the last update.
    u32 staleTiles = 0; ///< Loaded tiles whose input changed and that are not rebuilt (no rebuildAtRuntime).
    u32 agents = 0;     ///< NavAgents with a crowd slot.
    u32 moving = 0;     ///< Agents that moved in the last update.
    u32 pathSearches = 0;  ///< Path searches finished by the last update.
    u32 tileBuilds = 0;    ///< Tiles baked or rebuilt since construction.
    u32 failures = 0;      ///< Surfaces whose data or bake failed, since construction.
    u32 droppedEvents = 0; ///< Events dropped by the last update.
};

/// What NavWorld::drawDebug() draws.
struct NavDebugOptions
{
    bool navMesh = true;             ///< Polygon edges, coloured by area.
    bool tiles = false;              ///< Tile borders.
    bool links = true;               ///< Off-mesh links.
    bool agents = true;              ///< Agent cylinders and velocities.
    bool paths = true;               ///< The corridors of moving agents.
    bool obstacles = true;           ///< NavObstacles.
    u32 agentTypeMask = 0xFFFFFFFFu; ///< Navmeshes to draw (bit i = agent type i).
};

/// Receives the lines of NavWorld::drawDebug() (the editor's viewport adapts it to DebugDraw).
class NavDebugSink
{
public:
    /// A line from @p a to @p b in world space with a linear RGBA @p color.
    virtual void line(const Vec3& a, const Vec3& b, const Vec4& color) = 0;

protected:
    NavDebugSink() = default;
    virtual ~NavDebugSink() = default;
};

/// Keeps one tiled navmesh per NavMeshSurface of a World (one per agent type) and one crowd slot per
/// NavAgent. update() loads each surface's NavMeshSurface::data through the AssetStreamer (decoder
/// `illusion.navmesh`); a surface without data, with a rejected file or with stale tiles while
/// rebuildAtRuntime is baked in the background (collectNavGeometry() and the bake of NavBuild.hpp, at most
/// NavWorldDesc::maxTileBuildsInFlight tiles at once), and finished tiles are swapped in at the start of an
/// update. With rebuildAtRuntime, the tiles touched by changed, added or removed sources (Transform,
/// Collider, NavArea, NavLink, NavObstacle) are rebuilt. Live reload of a `.navmesh` file replaces its tiles.
///
/// The crowd runs, per update with @p advance: slots and positions are synchronised (Character agents from
/// where physics put them), changed destinations request paths, searches run as Jobs with a fixed iteration
/// budget, then steering, avoidance and integration run as Jobs per agent, each reading the previous phase
/// and writing only its own slot; NavAgentState, Transforms (Transform drive, link traversal, facing) and
/// events are written on the World's thread in Entity order. Deterministic for any number of workers with
/// fixed steps. Entities with the Inactive tag keep their slot and do not move. Allocates only when slots
/// are added, assets load or tiles are built. Use on the World's thread.
class NavWorld : NonCopyable
{
public:
    /// @p world and @p streamer (null: surfaces are baked, never loaded) must outlive this object.
    NavWorld(World& world, AssetStreamer* streamer, const NavWorldDesc& desc = {});
    ~NavWorld();

    /// Syncs surfaces and tiles, then, with @p advance, moves the agents by @p seconds (Jobs on @p jobs when
    /// given). Without @p advance (the editor outside Play mode) agents get slots and states but do not move.
    /// Adds NavAgentState, so call it outside queries.
    void update(f32 seconds, JobSystem* jobs, bool advance);

    /// Calls update(0, @p jobs, false) until no surface waits for its data or tiles.
    void waitIdle(JobSystem* jobs);

    /// True when the navmesh of @p agentType has every tile (none waits for a load or bake).
    [[nodiscard]] bool isReady(u32 agentType) const noexcept;

    /// @name Queries (World's thread; they never allocate)
    /// @{

    /// Writes the corner points of the shortest path from @p from to @p to into @p out (the first is the
    /// start on the navmesh). A path to an unreachable point ends at the nearest reachable one (partial);
    /// NoPath when nothing reachable is nearer to @p to than the start.
    [[nodiscard]] Expected<NavPathResult, NavError>
    findPath(const Vec3& from, const Vec3& to, std::span<Vec3> out, const NavQueryFilter& filter = {}) const;

    /// The point of the navmesh nearest @p position within NavQueryFilter::searchExtents.
    [[nodiscard]] Expected<Vec3, NavError> nearestPoint(const Vec3& position,
                                                        const NavQueryFilter& filter = {}) const;

    /// Walks the navmesh surface from @p from towards @p to and reports the first wall it meets.
    [[nodiscard]] Expected<NavRaycastHit, NavError> raycast(const Vec3& from, const Vec3& to,
                                                            const NavQueryFilter& filter = {}) const;

    /// A point of the navmesh within @p radius of @p center reachable from it, chosen by @p seed
    /// (the same seed gives the same point).
    [[nodiscard]] Expected<Vec3, NavError> randomPoint(const Vec3& center, f32 radius, u64 seed,
                                                       const NavQueryFilter& filter = {}) const;

    /// @}

    /// @name Agents
    /// @{

    /// Sets @p entity's NavAgent::destination; its path is searched in the next update.
    Expected<void, NavError> setDestination(Entity entity, const Vec3& destination);

    /// Clears @p entity's destination; the agent slows to a stop.
    Expected<void, NavError> stop(Entity entity);

    /// Moves @p entity to the navmesh point nearest @p position at once (its Transform), dropping its path.
    Expected<void, NavError> warp(Entity entity, const Vec3& position);

    /// The crowd slot of @p entity's NavAgent, or std::nullopt before its first update().
    [[nodiscard]] std::optional<u32> findSlot(Entity entity) const noexcept;

    /// The corridor corners of @p slot after the last update (for tools and debug drawing).
    [[nodiscard]] std::span<const Vec3> pathCorners(u32 slot) const noexcept;

    /// @}

    /// The events of the last update(), in Entity order.
    [[nodiscard]] std::span<const NavEvent> events() const noexcept;

    /// Marks the tiles overlapping @p bounds stale; surfaces with rebuildAtRuntime rebuild them.
    void invalidate(const Aabb& bounds);

    /// Draws what @p options select into @p sink.
    void drawDebug(const NavDebugOptions& options, NavDebugSink& sink) const;

    /// Counters.
    [[nodiscard]] const NavStatistics& statistics() const noexcept;

    /// The World it navigates.
    [[nodiscard]] World& world() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
