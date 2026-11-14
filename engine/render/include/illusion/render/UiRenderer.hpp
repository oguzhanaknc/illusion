#pragma once

/// @file
/// The UI pass (ADR 0057): draws a UiDrawData over a colour target after SceneRenderer::render().
///
/// @code
/// auto ui = illusion::UiRenderer::create(device, resources);
/// const illusion::UiTextureId atlas = ui->addFont(font);
/// // each frame, after the scene:
/// sceneRenderer->render(commands, target, world, view);
/// ui->render(commands, target, drawList.data());
/// @endcode

#include <illusion/render/RenderResources.hpp>
#include <illusion/ui/UiDrawList.hpp>

#include <memory>

namespace illusion
{

class Font;
class RenderDevice;

/// Creation parameters of a UiRenderer.
struct UiRendererDesc
{
    nvrhi::Format outputFormat = nvrhi::Format::SBGRA8_UNORM; ///< Format of the targets passed to render().
    u32 maxVertices = 65536; ///< Vertices of one render(); a larger UiDrawData is not drawn.
    u32 maxIndices = 98304;  ///< Indices of one render().
    u32 maxTextures = 256;   ///< Fonts and images registered at once.
};

/// Draws UI quads with premultiplied-alpha blending, in display colours, without depth: colour images and
/// signed distance field text in one pipeline. The device and the resources outlive it. Use on the render
/// thread.
class UiRenderer : NonCopyable
{
public:
    /// Creates the pipeline and buffers; null (logged) on GPU failure.
    [[nodiscard]] static std::unique_ptr<UiRenderer> create(RenderDevice& device, RenderResources& resources,
                                                            const UiRendererDesc& desc = {});
    ~UiRenderer();

    /// Uploads the atlas of @p font and returns its id; none when the font is empty or the table is full.
    [[nodiscard]] UiTextureId addFont(const Font& font);
    /// Registers a colour texture of the RenderResources; none for a stale Handle or a full table. The
    /// texture must stay alive until remove().
    [[nodiscard]] UiTextureId addImage(TextureHandle texture);
    /// Forgets @p texture (and releases a font's atlas once the GPU has finished with it). Ids are reused.
    void remove(UiTextureId texture);

    /// Records the draw of @p data over @p target without clearing it. Commands with an unknown texture are
    /// skipped. Returns false for a missing or incompatible target or data beyond the capacities. Steady
    /// frames do not allocate.
    bool render(nvrhi::ICommandList* commands, nvrhi::ITexture* target, const UiDrawData& data);

private:
    UiRenderer();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
