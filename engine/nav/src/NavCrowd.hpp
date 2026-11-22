#pragma once

// The crowd of NavWorld (docs/adr/0062-navigation-and-ai.md): one slot per NavAgent and the six phases of
// an update, deterministic for any number of workers.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavService.hpp>
#include <illusion/nav/NavWorld.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>

class dtNavMesh;

namespace illusion
{

class JobSystem;
class World;

namespace detail
{

/// The navmesh of one agent type as the crowd sees it.
struct NavCrowdSurface
{
    const dtNavMesh* mesh = nullptr;           // null while the type has no navmesh to walk
    u32 generation = 0;                        // changes when the navmesh object is replaced
    std::array<f32, kMaxNavAreas> areaCosts{}; // the surface's area costs
    std::span<const EntityGuid> links;         // NavLink Entities by off-mesh connection user id
};

/// The crowd: slots, path searches, steering, avoidance, integration and link traversal.
class NavCrowd : NonCopyable
{
public:
    NavCrowd(World& world, const NavWorldDesc& desc);
    ~NavCrowd();

    /// Runs the phases of one update; without @p advance only slots and states are synchronised.
    void update(f32 seconds, JobSystem* jobs, bool advance,
                std::span<const NavCrowdSurface, kMaxNavAgentTypes> surfaces, NavStatistics& statistics);

    /// Notes that warp() moved @p entity's Transform: its path is dropped and searched again.
    void warped(Entity entity) noexcept;

    [[nodiscard]] std::optional<u32> findSlot(Entity entity) const noexcept;
    [[nodiscard]] std::span<const Vec3> pathCorners(u32 slot) const noexcept;
    [[nodiscard]] std::span<const NavEvent> events() const noexcept;

    /// Agent cylinders and velocities (@p agents) and corridor corners (@p paths).
    void drawDebug(bool agents, bool paths, u32 agentTypeMask, NavDebugSink& sink) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace detail
} // namespace illusion
