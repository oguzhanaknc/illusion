#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavWorldService.hpp>

#include <algorithm>

namespace illusion
{

NavWorldService::~NavWorldService() = default;

Expected<NavPathResult, NavError> NavWorldService::findPath(const Vec3& from, const Vec3& to,
                                                            std::span<Vec3> out, const NavQueryFilter& filter)
{
    return m_navigation.findPath(from, to, out, filter);
}

Expected<Vec3, NavError> NavWorldService::nearestPoint(const Vec3& position, const NavQueryFilter& filter)
{
    return m_navigation.nearestPoint(position, filter);
}

Expected<NavRaycastHit, NavError> NavWorldService::raycast(const Vec3& from, const Vec3& to,
                                                           const NavQueryFilter& filter)
{
    return m_navigation.raycast(from, to, filter);
}

Expected<Vec3, NavError> NavWorldService::randomPoint(const Vec3& center, f32 radius, u64 seed,
                                                      const NavQueryFilter& filter)
{
    return m_navigation.randomPoint(center, radius, seed, filter);
}

Expected<void, NavError> NavWorldService::setDestination(Entity entity, const Vec3& destination)
{
    return m_navigation.setDestination(entity, destination);
}

Expected<void, NavError> NavWorldService::stop(Entity entity)
{
    return m_navigation.stop(entity);
}

Expected<void, NavError> NavWorldService::warp(Entity entity, const Vec3& position)
{
    return m_navigation.warp(entity, position);
}

Expected<NavAgentStatus, NavError> NavWorldService::status(Entity entity) const
{
    const World& world = m_navigation.world();
    const NavAgent* agent = world.isAlive(entity) ? world.tryGet<NavAgent>(entity) : nullptr;
    if (agent == nullptr)
    {
        return unexpected(NavError::NoAgent);
    }
    if (const NavAgentState* state = world.tryGet<NavAgentState>(entity))
    {
        return state->status;
    }
    return agent->destination ? NavAgentStatus::Pending : NavAgentStatus::Idle;
}

Expected<f32, NavError> NavWorldService::remainingDistance(Entity entity) const
{
    const World& world = m_navigation.world();
    if (!world.isAlive(entity) || !world.has<NavAgent>(entity))
    {
        return unexpected(NavError::NoAgent);
    }
    const NavAgentState* state = world.tryGet<NavAgentState>(entity);
    return state != nullptr ? state->remainingDistance : 0.0f;
}

u32 NavWorldService::pollEvents(std::span<NavEvent> out)
{
    const std::span<const NavEvent> events = m_navigation.events();
    const usize count = std::min(out.size(), events.size());
    std::copy_n(events.begin(), count, out.begin());
    return static_cast<u32>(events.size());
}

} // namespace illusion
