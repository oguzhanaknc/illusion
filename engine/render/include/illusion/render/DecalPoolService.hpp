#pragma once

/// @file
/// The host's implementation of DecalService over DecalPool.
///
/// @code
/// illusion::DecalPool decals(world);
/// illusion::DecalPoolService decalService(decals);
/// services.provide<illusion::DecalService>(decalService);
/// @endcode

#include <illusion/render/DecalPool.hpp>
#include <illusion/render/DecalService.hpp>

namespace illusion
{

/// DecalService that forwards every call to a DecalPool, which must outlive it. Created by the host; a shared
/// module that constructs one fails to link, since only the host has the library's code (ADR 0041).
class DecalPoolService final : public DecalService
{
public:
    /// Serves @p decals.
    explicit DecalPoolService(DecalPool& decals) noexcept
        : m_decals(decals)
    {
    }
    ~DecalPoolService() override;

    /// The DecalPool it serves.
    [[nodiscard]] DecalPool& decals() const noexcept { return m_decals; }

    /// @name DecalService
    /// @{
    Expected<DecalMaterial, DecalError> material(const AssetRef& material) override;
    Expected<DecalHandle, DecalError> spawn(const DecalSpawn& decal) override;
    Expected<void, DecalError> fadeOut(DecalHandle decal, f32 seconds) override;
    Expected<void, DecalError> remove(DecalHandle decal) override;
    void clear() override;
    [[nodiscard]] bool isAlive(DecalHandle decal) const override;
    [[nodiscard]] u32 aliveCount() const override;
    [[nodiscard]] u32 capacity() const override;
    /// @}

private:
    DecalPool& m_decals;
};

} // namespace illusion
