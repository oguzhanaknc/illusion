#pragma once

/// @file
/// The decals game code spawns at run time (docs/adr/0066-decals.md), kept outside the ECS in a fixed number
/// of slots: a GameSession owns one, modules reach it through DecalService, and SceneRenderer draws it
/// through RenderView::decals.
///
/// @code
/// illusion::DecalPool decals(world, {.capacity = 2048, .materialCapacity = 64});
/// const illusion::DecalMaterial scorch =
///     decals.material(illusion::AssetRef("game:/decals/scorch.material")).value();
/// static_cast<void>(decals.spawn({.material = scorch, .position = point, .rotation = rotation}));
/// decals.update(deltaSeconds);    // after the Hierarchy step
/// resolver.resolveDecals(decals); // with the render assets
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/render/DecalComponents.hpp>
#include <illusion/render/RenderResources.hpp>

#include <memory>
#include <span>

namespace illusion
{

class World;

/// Creation parameters of a DecalPool.
struct DecalPoolDesc
{
    u32 capacity = 1024;       ///< Live decals before the oldest is replaced, 1 to 65536.
    u32 materialCapacity = 64; ///< Distinct decal materials, 1 to 1024.
};

/// One slot of a DecalPool, as the renderer and tools read it.
struct PooledDecal
{
    DecalHandle handle{};      ///< The handle spawn() returned for it.
    bool alive = false;        ///< Spawned and not retired; the other fields are stale otherwise.
    bool hidden = false;       ///< Its attachment has the Inactive tag; kept, not drawn.
    u64 sequence = 0;          ///< Spawn order since the pool was created, from 1 (draw order among equals).
    DecalMaterial material{};  ///< Its material.
    Mat4 world{1.0f};          ///< World placement of the box centre, without size, after the last update().
    Vec3 size{1.0f};           ///< Box extents in metres.
    Vec4 color{1.0f};          ///< Linear colour.
    f32 opacity = 1.0f;        ///< Coverage from the spawn.
    f32 fade = 1.0f;           ///< Current weight of its fade-in and fade-out, 0 to 1.
    f32 age = 0.0f;            ///< Seconds since it was spawned, in update() steps.
    f32 lifetime = 0.0f;       ///< Seconds until it fades out; 0: until evicted or removed.
    f32 fadeInSeconds = 0.0f;  ///< Seconds it takes to appear.
    f32 fadeOutSeconds = 1.0f; ///< Seconds it takes to disappear.
    f32 fadeOutAge = 0.0f;     ///< Age at which fading out began; 0 while it has not.
    i32 sortOrder = 0;         ///< Draw order.
    u32 receiverLayers = kAllDecalLayers; ///< Decal layers it lands on.
    f32 angleFadeStart = 1.0471976f;      ///< Radians; see Decal::angleFadeStart.
    f32 angleFadeEnd = 1.3962634f;        ///< Radians; see Decal::angleFadeEnd.
    f32 depthFade = 0.1f;                 ///< See Decal::depthFade.
    u32 variant = 0;                      ///< Atlas frame or other variation.
    Entity attachTo{};                    ///< Entity it follows; null in world space.
    Mat4 attachment{1.0f};                ///< Its placement relative to attachTo's Transform.
};

/// Counters of a DecalPool.
struct DecalPoolStatistics
{
    u32 alive = 0;     ///< Live decals.
    u32 materials = 0; ///< Interned materials.
    u64 spawned = 0;   ///< Decals spawned since construction.
    u64 evicted = 0;   ///< Live decals replaced because the pool was full, since construction.
    u64 retired = 0;   ///< Decals that faded out, were removed or lost their attachment, since construction.
};

/// Fixed-capacity pool of spawned decals over one World, which must outlive it. A spawn takes a free slot or,
/// when none is free, replaces the oldest live decal; nothing allocates after construction except a
/// material's first material() call. Results depend only on the calls and the update steps. Not thread-safe:
/// use it on the thread that updates the World.
class DecalPool : NonCopyable
{
public:
    /// Reserves @p desc capacity slots for decals over @p world.
    explicit DecalPool(World& world, const DecalPoolDesc& desc = {});
    ~DecalPool();

    /// The handle of @p material, interned on first use.
    [[nodiscard]] Expected<DecalMaterial, DecalError> material(const AssetRef& material);
    /// Spawns @p decal; an attached one keeps its placement relative to the attachment's current Transform.
    [[nodiscard]] Expected<DecalHandle, DecalError> spawn(const DecalSpawn& decal);
    /// Starts fading @p decal out over @p seconds (0: it retires at the next update()).
    [[nodiscard]] Expected<void, DecalError> fadeOut(DecalHandle decal, f32 seconds);
    /// Retires @p decal at once.
    [[nodiscard]] Expected<void, DecalError> remove(DecalHandle decal);
    /// Retires every decal; materials stay interned.
    void clear() noexcept;

    /// Ages every decal by @p deltaSeconds, applies the fades, retires finished decals and those whose
    /// attachment is gone, hides those whose attachment is Inactive and places attached ones. Does not
    /// allocate.
    void update(f32 deltaSeconds);

    /// Whether @p decal is live.
    [[nodiscard]] bool isAlive(DecalHandle decal) const noexcept;
    /// Live decals.
    [[nodiscard]] u32 aliveCount() const noexcept;
    /// Slots of the pool.
    [[nodiscard]] u32 capacity() const noexcept;
    /// Every slot, live or not, in slot order.
    [[nodiscard]] std::span<const PooledDecal> slots() const noexcept;
    /// The interned material references; DecalMaterial index i is element i - 1.
    [[nodiscard]] std::span<const AssetRef> materials() const noexcept;
    /// The GPU material of @p material as the last RenderAssetResolver::resolveDecals() left it (none while
    /// it loads or after it failed).
    [[nodiscard]] MaterialHandle resolvedMaterial(DecalMaterial material) const noexcept;
    /// Records the GPU material of @p material (called by RenderAssetResolver::resolveDecals()).
    void setResolvedMaterial(DecalMaterial material, MaterialHandle handle) noexcept;
    /// Counters.
    [[nodiscard]] DecalPoolStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
