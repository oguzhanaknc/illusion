#pragma once

/// @file
/// The navigation part of the scripting API (docs/adr/0062-navigation-and-ai.md): game modules query the
/// navmeshes, send NavAgents to destinations and read the events of the last update.
///
/// @code
/// if (illusion::NavService* nav = context.service<illusion::NavService>())
/// {
///     if (auto point = nav->randomPoint(home, 10.0f, seed))
///     {
///         static_cast<void>(nav->setDestination(creature, *point));
///     }
///     std::array<illusion::NavEvent, 16> events{};
///     const illusion::u32 count = nav->pollEvents(events);
///     for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
///     {
///         if (events[i].type == illusion::NavEventType::LinkEntered)
///         {
///             playJump(events[i].agent);
///         }
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/nav/NavComponents.hpp>

#include <span>
#include <string_view>

namespace illusion
{

/// Why a navigation call failed.
enum class NavError : u8
{
    NoAgent,      ///< The Entity has no NavAgent (or is not alive).
    NoNavMesh,    ///< No navmesh of that agent type is loaded yet.
    OffMesh,      ///< No navmesh polygon within the search extents of a point.
    NoPath,       ///< The points are on the navmesh but nothing connects them.
    InvalidValue, ///< A value is not finite or out of range (an agent type, a negative radius, an empty
                  ///< span).
};

/// Stable name of @p error, e.g. "OffMesh".
[[nodiscard]] constexpr std::string_view navErrorName(NavError error) noexcept
{
    switch (error)
    {
    case NavError::NoAgent:
        return "NoAgent";
    case NavError::NoNavMesh:
        return "NoNavMesh";
    case NavError::OffMesh:
        return "OffMesh";
    case NavError::NoPath:
        return "NoPath";
    case NavError::InvalidValue:
        return "InvalidValue";
    }
    return "InvalidValue";
}

/// Which navmesh a query uses and what it may cross.
struct NavQueryFilter
{
    u32 agentType = 0;                    ///< Navmesh of this agent type.
    u32 areaMask = 0xFFFFFFFFu;           ///< Areas it may cross (bit i = area i).
    Vec3 searchExtents{2.0f, 4.0f, 2.0f}; ///< Half size of the box searched for the polygon of a point, in m.
};

/// Result of a path query.
struct NavPathResult
{
    u32 count = 0;        ///< Corner points written (at most the span's size).
    u32 total = 0;        ///< Corner points of the whole path (more than count when the span was small).
    f32 length = 0.0f;    ///< Length of the whole path in m.
    bool partial = false; ///< The path ends at the reachable point nearest the destination.
};

/// Result of a navmesh raycast.
struct NavRaycastHit
{
    bool hit = false;    ///< A wall was met before the end point.
    Vec3 point{0.0f};    ///< Where the walk stopped (the end point without a hit).
    Vec3 normal{0.0f};   ///< Horizontal normal of the wall (zero without a hit).
    f32 fraction = 1.0f; ///< Fraction of the way walked, in [0, 1].
};

/// Kind of NavEvent.
enum class NavEventType : u8
{
    Arrived,     ///< The agent reached its destination.
    PathFailed,  ///< No path to the destination was found.
    LinkEntered, ///< The agent started crossing a NavLink.
    LinkExited,  ///< The agent finished crossing a NavLink.
};

/// Something an agent did in the last update.
struct NavEvent
{
    Entity agent;                              ///< The NavAgent's Entity.
    NavEventType type = NavEventType::Arrived; ///< What happened.
    Entity link;                               ///< Link events: the NavLink's Entity.
    NavLinkKind kind = NavLinkKind::Jump;      ///< Link events: the link's kind.
    Vec3 position{0.0f};                       ///< The agent's position in world space.
};

/// Navigation of the running game, implemented by the host (NavWorldService over a NavWorld) and found by
/// modules with `context.service<NavService>()`. Destination changes take effect at the next Navigation
/// step. Adding a function changes the module API version.
class NavService : NonCopyable
{
public:
    /// Writes the corners of the shortest path from @p from to @p to into @p out.
    virtual Expected<NavPathResult, NavError> findPath(const Vec3& from, const Vec3& to, std::span<Vec3> out,
                                                       const NavQueryFilter& filter) = 0;

    /// The navmesh point nearest @p position.
    [[nodiscard]] virtual Expected<Vec3, NavError> nearestPoint(const Vec3& position,
                                                                const NavQueryFilter& filter) = 0;

    /// Walks the navmesh from @p from towards @p to and reports the first wall.
    [[nodiscard]] virtual Expected<NavRaycastHit, NavError> raycast(const Vec3& from, const Vec3& to,
                                                                    const NavQueryFilter& filter) = 0;

    /// A reachable navmesh point within @p radius of @p center, the same for the same @p seed.
    [[nodiscard]] virtual Expected<Vec3, NavError> randomPoint(const Vec3& center, f32 radius, u64 seed,
                                                               const NavQueryFilter& filter) = 0;

    /// Sends @p entity's NavAgent to @p destination (sets NavAgent::destination, so it is saved).
    virtual Expected<void, NavError> setDestination(Entity entity, const Vec3& destination) = 0;

    /// Clears @p entity's destination.
    virtual Expected<void, NavError> stop(Entity entity) = 0;

    /// Moves @p entity to the navmesh point nearest @p position at once, dropping its path.
    virtual Expected<void, NavError> warp(Entity entity, const Vec3& position) = 0;

    /// What @p entity's NavAgent did in the last update.
    [[nodiscard]] virtual Expected<NavAgentStatus, NavError> status(Entity entity) const = 0;

    /// Distance left along @p entity's path in m.
    [[nodiscard]] virtual Expected<f32, NavError> remainingDistance(Entity entity) const = 0;

    /// Writes the events of the last update into @p out, in Entity order, and returns how many there are;
    /// every call in an update returns the same events.
    virtual u32 pollEvents(std::span<NavEvent> out) = 0;

protected:
    NavService() = default;
    virtual ~NavService() = default;
};

} // namespace illusion
