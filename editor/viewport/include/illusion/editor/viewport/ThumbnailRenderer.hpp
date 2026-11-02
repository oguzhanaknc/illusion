#pragma once

/// @file
/// The thumbnails of `asset.thumbnail`: models and material files rendered in a preview World by a small
/// SceneRenderer, environments as the background of that World, and textures scaled down on the CPU. It is
/// the ThumbnailHost of the editor process; ViewportService owns one, shares its asset cache with it and
/// updates it every frame.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/ThumbnailHost.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <memory>

namespace illusion
{
class AssetStreamer;
class JobSystem;
class RenderAssetCache;
class RenderDevice;
class RenderResources;
struct Image;
} // namespace illusion

namespace illusion::editor::viewport
{

/// Counters of a ThumbnailRenderer.
struct ThumbnailRendererStats
{
    u32 rendered = 0;             ///< Preview Worlds rendered on the GPU since creation.
    u32 completed = 0;            ///< Requests answered (with an image or an error) since creation.
    u32 pending = 0;              ///< Requests not answered yet.
    u32 steps = 0;                ///< Thumbnails rendered or scaled by the last update() (0 or 1).
    f64 updateMilliseconds = 0.0; ///< CPU time of the last update().
};

/// The thumbnail of @p image (any ImageFormat, with or without mips): scaled to fit a @p size square with
/// its aspect kept, box-filtered from the smallest mip at least that large (enlarged without filtering when
/// smaller), composited over a grey checkerboard by its alpha. Linear 8-bit images are shown as stored;
/// float images are tonemapped (Reinhard) and sRGB-encoded.
[[nodiscard]] ThumbnailImage textureThumbnail(const Image& image, u32 size = kThumbnailSize);

/// The camera that frames @p bounds in a square thumbnail: looking down at it from the front right with a
/// 30 degree field of view (a unit box at the origin for an invalid box).
[[nodiscard]] ViewportCamera thumbnailCamera(const Aabb& bounds) noexcept;

/// Makes thumbnails one at a time, in the order requested, with at most one render (or texture scale) per
/// update(): a preview World of its own, resolved through a shared RenderAssetCache, rendered at
/// kThumbnailSize into an RGBA8 target and read back without waiting. Use on the main (render) thread.
class ThumbnailRenderer final : public ThumbnailHost
{
public:
    /// Creates the renderer. @p device, @p resources, @p cache, @p streamer and @p jobs must outlive it.
    /// Returns nullptr (and logs why) if a GPU object cannot be created.
    [[nodiscard]] static std::unique_ptr<ThumbnailRenderer> create(RenderDevice& device,
                                                                   RenderResources& resources,
                                                                   RenderAssetCache& cache,
                                                                   AssetStreamer& streamer, JobSystem& jobs);
    ~ThumbnailRenderer() override;

    ThumbnailRenderer(const ThumbnailRenderer&) = delete;            ///< Not copyable.
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete; ///< Not copyable.

    /// Advances the current request by one step: starts loading it, resolves its preview World, renders it
    /// or reads its image back. Call once per frame after RenderAssetCache::update(). Does nothing (and
    /// allocates nothing) without requests.
    void update();

    /// Updates, waiting for loads and the GPU, until every request is answered (for tests and tools).
    void waitIdle();

    /// Counters.
    [[nodiscard]] const ThumbnailRendererStats& stats() const noexcept;

    // ThumbnailHost
    [[nodiscard]] Expected<u64, CommandError> requestThumbnail(const ThumbnailRequest& request) override;
    [[nodiscard]] std::optional<Expected<ThumbnailImage, CommandError>> thumbnail(u64 ticket) override;
    void cancelThumbnail(u64 ticket) override;

    /// The renderer's state, defined in its sources.
    struct Impl;

private:
    explicit ThumbnailRenderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor::viewport
