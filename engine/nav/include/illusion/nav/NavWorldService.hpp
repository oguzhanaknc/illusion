#pragma once

/// @file
/// The host's implementation of NavService over NavWorld.
///
/// @code
/// illusion::NavWorld navigation(world, &streamer);
/// illusion::NavWorldService navService(navigation);
/// services.provide<illusion::NavService>(navService);
/// @endcode

#include <illusion/nav/NavService.hpp>
#include <illusion/nav/NavWorld.hpp>

namespace illusion
{

/// NavService over a NavWorld, which must outlive it. Created by the host; a shared module that constructs
/// one fails to link, since only the host has the library's code (ADR 0041).
class NavWorldService final : public NavService
{
public:
    /// Serves @p navigation.
    explicit NavWorldService(NavWorld& navigation) noexcept
        : m_navigation(navigation)
    {
    }
    ~NavWorldService() override;

    /// The NavWorld it serves.
    [[nodiscard]] NavWorld& navigation() const noexcept { return m_navigation; }

    /// @name NavService
    /// @{
    Expected<NavPathResult, NavError> findPath(const Vec3& from, const Vec3& to, std::span<Vec3> out,
                                               const NavQueryFilter& filter) override;
    [[nodiscard]] Expected<Vec3, NavError> nearestPoint(const Vec3& position,
                                                        const NavQueryFilter& filter) override;
    [[nodiscard]] Expected<NavRaycastHit, NavError> raycast(const Vec3& from, const Vec3& to,
                                                            const NavQueryFilter& filter) override;
    [[nodiscard]] Expected<Vec3, NavError> randomPoint(const Vec3& center, f32 radius, u64 seed,
                                                       const NavQueryFilter& filter) override;
    Expected<void, NavError> setDestination(Entity entity, const Vec3& destination) override;
    Expected<void, NavError> stop(Entity entity) override;
    Expected<void, NavError> warp(Entity entity, const Vec3& position) override;
    [[nodiscard]] Expected<NavAgentStatus, NavError> status(Entity entity) const override;
    [[nodiscard]] Expected<f32, NavError> remainingDistance(Entity entity) const override;
    u32 pollEvents(std::span<NavEvent> out) override;
    /// @}

private:
    NavWorld& m_navigation;
};

} // namespace illusion
