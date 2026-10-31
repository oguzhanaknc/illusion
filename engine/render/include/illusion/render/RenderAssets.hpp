#pragma once

/// @file
/// Resolving asset references for the renderer: MeshRenderer and SkyLight (SceneComponents.hpp) become
/// RenderMesh and RenderSky (Components.hpp) once their meshes, materials, textures and environments are
/// loaded and on the GPU.
///
/// A RenderAssetCache owns the loaded assets and is shared by every World drawn with the same
/// RenderResources; each World gets a RenderAssetResolver. Call both once per frame on the render thread,
/// after AssetStreamer::update() and before SceneRenderer::render():
///
/// @code
/// illusion::RenderAssetCache cache(resources, streamer);
/// illusion::RenderAssetResolver resolver(cache);
/// while (running)
/// {
///     streamer.update();
///     cache.update();
///     resolver.update(world, &jobs);
///     renderer->render(commandList, target, world, view, &jobs);
/// }
/// @endcode
///
/// Renderers whose assets are still loading keep what they drew before (nothing for new ones). A mesh that
/// fails to load draws nothing; a material that fails draws with the default material. See
/// docs/adr/0035-asset-references.md.
///
/// Live reload: AssetStreamer::reload() of a file makes the cache load every asset made from it again. The
/// previous version stays on the GPU until the new one is there (and stays if the reload fails); material
/// files change in place through RenderResources::updateMaterial(), other assets get new handles and every
/// resolver resolves its renderers again. Assets no resolver uses are freed by evictUnused(). See
/// docs/adr/0045-asset-catalog-and-file-watching.md.

#include <illusion/core/AssetId.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/render/RenderResources.hpp>

#include <memory>
#include <string_view>

namespace illusion
{

class AssetStreamer;
class JobSystem;
class World;
struct MeshRenderer;
struct ProceduralSky;

/// Creation parameters of a RenderAssetCache.
struct RenderAssetCacheDesc
{
    u64 uploadBytesPerUpdate = 64ull << 20; ///< GPU bytes of streamed models uploaded per update().
    EnvironmentDesc environment{};          ///< Bake resolution of environments.
};

/// Counters of a RenderAssetCache.
struct RenderAssetCacheStatistics
{
    u32 models = 0;       ///< Models on the GPU.
    u32 meshes = 0;       ///< Mesh assets ready (built-in meshes and meshes of models).
    u32 materials = 0;    ///< Material assets ready.
    u32 textures = 0;     ///< Textures of material files on the GPU.
    u32 environments = 0; ///< Environments baked.
    u32 loading = 0;      ///< Assets still loading (first loads, reloads and files waiting to reload).
    u32 failed = 0;       ///< Assets that failed to load (see RenderAssetCache::error()).
    u32 reloads = 0;      ///< Reloads finished since construction (see AssetStreamer::reload()).
    u32 evicted = 0;      ///< Assets freed by RenderAssetCache::evictUnused() since construction.
};

/// The assets loaded for the renderer, shared by the RenderAssetResolvers of several Worlds. Assets are
/// requested by resolvers, loaded through an AssetStreamer and uploaded to RenderResources (streamed models
/// through an UploadQueue with a byte budget per update). It counts how many resolvers use each asset.
/// Use on the render thread.
class RenderAssetCache : NonCopyable
{
public:
    /// @p resources and @p streamer must outlive the cache.
    RenderAssetCache(RenderResources& resources, AssetStreamer& streamer,
                     const RenderAssetCacheDesc& desc = {});
    ~RenderAssetCache();

    /// Starts the reloads asked for by AssetStreamer::reload() since the last call, uploads loaded models
    /// within the byte budget, finishes materials and environments whose files are loaded and lets
    /// RenderResources free what was released (RenderResources::collect()). Call once per frame after
    /// AssetStreamer::update().
    void update();

    /// Frees the GPU resources of assets no resolver uses (see RenderAssetResolver::releaseUnused()), and of
    /// models and textures only those assets used. Assets still loading are kept. Returns the number of
    /// assets freed; requesting one again loads it again.
    u32 evictUnused();

    /// Updates the streamer and the cache until no asset is loading (for loading screens, tools, tests).
    void waitIdle();

    /// Number of assets still loading.
    [[nodiscard]] u32 loading() const noexcept;

    /// Counters.
    [[nodiscard]] RenderAssetCacheStatistics statistics() const noexcept;

    /// Number of resolvers that use the mesh, material or environment asset @p asset (0 if none or unknown).
    /// A resolver uses an asset from its first request until releaseUnused() finds no renderer with it.
    [[nodiscard]] u32 references(AssetId asset) const noexcept;

    /// Why @p asset failed to load (empty if it did not fail or is unknown).
    [[nodiscard]] std::string_view error(AssetId asset) const noexcept;

    /// @cond INTERNAL
    struct Impl;
    [[nodiscard]] Impl& impl() noexcept { return *m_impl; }
    /// @endcond

private:
    std::unique_ptr<Impl> m_impl;
};

/// Counters of a RenderAssetResolver.
struct RenderAssetResolverStatistics
{
    u32 resolved = 0;   ///< Renderers and sky lights (re)resolved by the last update().
    u32 reresolves = 0; ///< update() calls that resolved everything again after a reload, since construction.
    u32 pending = 0;    ///< Renderers and sky lights waiting for assets after the last update().
    u32 failures = 0;   ///< Renderers and sky lights resolved with an asset that failed, since construction.
};

/// Keeps the RenderMesh and RenderSky Components of one World in step with its MeshRenderer and SkyLight
/// Components. Every update() compares each MeshRenderer with the key its RenderMesh was resolved from
/// (in parallel when given a Job System) and resolves only the ones that differ, so a World whose assets are
/// all resolved costs a few nanoseconds per renderer and allocates nothing. Entities with the Inactive tag
/// are skipped. Use on the render thread.
class RenderAssetResolver : NonCopyable
{
public:
    /// Resolves through @p cache, which must outlive the resolver.
    explicit RenderAssetResolver(RenderAssetCache& cache);

    /// Releases the resolver's references in the cache.
    ~RenderAssetResolver();

    /// Adds RenderMesh to new MeshRenderers (and RenderSky to new SkyLights with an environment), removes
    /// the ones whose source Component is gone, and re-resolves those whose source changed.
    void update(World& world, JobSystem* jobs = nullptr);

    /// Calls update() and RenderAssetCache::waitIdle() until nothing in @p world waits for assets.
    void waitIdle(World& world, JobSystem* jobs = nullptr);

    /// Stops using the assets no MeshRenderer or SkyLight of @p world refers to any more, so that
    /// RenderAssetCache::evictUnused() can free them. Walks every renderer: call it when assets may have
    /// gone (a scene was replaced, a play session ended), not every frame. Returns the number of assets
    /// released.
    u32 releaseUnused(World& world);

    /// Counters.
    [[nodiscard]] const RenderAssetResolverStatistics& statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Key a RenderMesh resolved from @p renderer stores in RenderMesh::sourceKey (never 0).
[[nodiscard]] u64 renderSourceKey(const MeshRenderer& renderer) noexcept;

} // namespace illusion
