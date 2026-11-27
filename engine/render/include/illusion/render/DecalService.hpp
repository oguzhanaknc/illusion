#pragma once

/// @file
/// The decal part of the scripting API (docs/adr/0066-decals.md): game modules spawn decals at run time
/// (bullet holes, blood, scorch marks, footprints) into the host's pool, which keeps at most its capacity and
/// replaces the oldest when full.
///
/// @code
/// // start(): intern the materials once.
/// m_hole = decals->material(illusion::AssetRef("game:/decals/bullet_hole.material")).value();
/// // update(): mark the hit.
/// static_cast<void>(decals->spawn({.material = m_hole,
///                                  .position = hit.point,
///                                  .rotation = illusion::decalRotation(hit.normal, spin),
///                                  .lifetime = 30.0f,
///                                  .variant = shot % 4,
///                                  .attachTo = hit.entity}));
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/render/DecalComponents.hpp>

namespace illusion
{

/// Decals spawned by game code in the running game, implemented by the host (DecalPoolService over a
/// DecalPool) and found by modules with `context.service<DecalService>()`. Spawned decals are drawn from the
/// next frame and are not saved in saved games. Adding a function changes the module API version.
/// @ingroup scripting_api
class DecalService : NonCopyable
{
public:
    /// The pool's handle for the decal material @p material, interned on first use (the same reference gives
    /// the same handle).
    virtual Expected<DecalMaterial, DecalError> material(const AssetRef& material) = 0;
    /// Spawns @p decal, replacing the oldest live decal when the pool is full.
    virtual Expected<DecalHandle, DecalError> spawn(const DecalSpawn& decal) = 0;
    /// Starts fading @p decal out over @p seconds (0: it retires at the next update), then retires it.
    virtual Expected<void, DecalError> fadeOut(DecalHandle decal, f32 seconds) = 0;
    /// Retires @p decal at once.
    virtual Expected<void, DecalError> remove(DecalHandle decal) = 0;
    /// Retires every spawned decal; materials stay interned.
    virtual void clear() = 0;

    /// Whether @p decal is still live (spawned, not retired, slot not reused).
    [[nodiscard]] virtual bool isAlive(DecalHandle decal) const = 0;
    /// Live spawned decals.
    [[nodiscard]] virtual u32 aliveCount() const = 0;
    /// Decals the pool holds before it replaces the oldest.
    [[nodiscard]] virtual u32 capacity() const = 0;

protected:
    DecalService() = default;
    virtual ~DecalService() = default;
};

} // namespace illusion
