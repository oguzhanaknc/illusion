#pragma once

/// @file
/// The fonts and images of the game UI on the GPU (ADR 0057): a UiAssets that loads them through an
/// AssetStreamer and registers them with a UiRenderer.
///
/// @code
/// auto uiRenderer = illusion::UiRenderer::create(device, resources);
/// illusion::UiRenderAssets uiAssets(streamer, resources, *uiRenderer);
/// illusion::UiWorld ui(world);
/// ui.setAssets(&uiAssets);
/// // each frame, after streamer.update():
/// ui.update({targetSize});
/// uiRenderer->render(commands, target, ui.drawData());
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <memory>

namespace illusion
{

class AssetStreamer;
class RenderResources;
class UiRenderer;

/// Capacities of a UiRenderAssets, reserved at construction.
struct UiRenderAssetsDesc
{
    u32 maxFonts = 32;   ///< Distinct fonts requested; later ones draw nothing (logged once).
    u32 maxImages = 512; ///< Distinct images requested; later ones draw nothing (logged once).
};

/// Counters of a UiRenderAssets.
struct UiRenderAssetsStatistics
{
    u32 fonts = 0;   ///< Fonts on the GPU.
    u32 images = 0;  ///< Images on the GPU.
    u32 loading = 0; ///< Fonts and images still loading.
    u32 failed = 0;  ///< Fonts and images that failed to load or to register.
};

/// Resolves the AssetRefs of UI Components: fonts with AssetStreamer::loadFont(), images with
/// AssetStreamer::loadImage() (sRGB, uploaded whole to the RenderResources, never mip-streamed), both
/// registered with the UiRenderer. The first request of an asset starts its load; until it is loaded nothing
/// is returned, and an asset that fails is logged once and never drawn. Lookups of requested assets go
/// through pre-reserved tables and do not allocate. The streamer, the resources and the renderer outlive it;
/// the host calls AssetStreamer::update(). Use on the render thread.
class UiRenderAssets final : public UiAssets
{
public:
    /// Works with @p streamer, @p resources and @p renderer.
    UiRenderAssets(AssetStreamer& streamer, RenderResources& resources, UiRenderer& renderer,
                   const UiRenderAssetsDesc& desc = {});
    /// Removes its textures from the renderer and releases the images' GPU textures.
    ~UiRenderAssets() override;

    /// The font of @p font once it is loaded and its atlas is on the GPU.
    [[nodiscard]] UiFontFace font(const AssetRef& font) override;
    /// The image of @p image once it is loaded and on the GPU.
    [[nodiscard]] UiImageSource image(const AssetRef& image) override;

    /// Counters.
    [[nodiscard]] UiRenderAssetsStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
