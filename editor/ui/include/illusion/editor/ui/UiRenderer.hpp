#pragma once

/// @file
/// Draws Dear ImGui's draw data with NVRHI and manages the textures Dear ImGui asks for (the dynamic font
/// atlas), plus textures the editor shows in the UI (viewports, thumbnails).
///
/// The UI is drawn in gamma space into an RGBA8_UNORM UI target; present() copies that target to the
/// swapchain and decodes it for _SRGB swapchains, so both kinds of swapchain show the same colours.
///
/// ```cpp
/// commands->open();
/// commands->clearTextureFloat(uiTarget, nvrhi::AllSubresources, clearColor);
/// renderer->render(commands, uiFramebuffer, ImGui::GetDrawData());
/// renderer->present(commands, uiTarget, swapchain.currentFramebuffer());
/// commands->close();
/// ```

#include <illusion/core/Base.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>

struct ImDrawData;

namespace illusion
{
class RenderDevice;
} // namespace illusion

namespace illusion::editor::ui
{

class UiContext;

/// Creation parameters of UiRenderer.
struct UiRendererDesc
{
    /// Format of the framebuffers render() draws into (the UI target).
    nvrhi::Format targetFormat = nvrhi::Format::RGBA8_UNORM;
    /// Frames the GPU may still be working on; vertex and index buffers are kept once per frame.
    u32 framesInFlight = 2;
};

/// Dear ImGui renderer backend on NVRHI. Uses 16-bit indices with vertex offsets
/// (ImGuiBackendFlags_RendererHasVtxOffset) and handles texture requests
/// (ImGuiBackendFlags_RendererHasTextures). Buffers grow but never shrink, so steady frames do not allocate.
/// Not thread-safe; use it with its UiContext on the UI thread.
class UiRenderer : NonCopyable
{
public:
    /// Creates the renderer for @p context. Returns nullptr (and logs why) if a GPU object cannot be created.
    /// @p device and @p context must outlive the renderer.
    [[nodiscard]] static std::unique_ptr<UiRenderer> create(RenderDevice& device, UiContext& context,
                                                            const UiRendererDesc& desc = {});

    /// Destroys every texture created for Dear ImGui and unregisters the backend from the context.
    ~UiRenderer();

    /// Processes the texture requests of @p drawData and records its draw commands into @p commands,
    /// drawing into @p framebuffer (its colour attachment must have the target format). @p drawData may be
    /// null or empty.
    void render(nvrhi::ICommandList* commands, nvrhi::IFramebuffer* framebuffer, ImDrawData* drawData);

    /// Copies @p uiTarget (an RGBA8_UNORM texture the size of @p output) to @p output, decoding sRGB when the
    /// output format is an _SRGB format.
    void present(nvrhi::ICommandList* commands, nvrhi::ITexture* uiTarget, nvrhi::IFramebuffer* output);

    /// Makes @p texture usable with ImGui::Image(). The texture must stay alive until unregisterTexture().
    [[nodiscard]] u64 registerTexture(nvrhi::ITexture* texture);

    /// Forgets a texture registered with registerTexture().
    void unregisterTexture(u64 textureId);

    /// Number of live textures: those Dear ImGui asked for plus registered ones.
    [[nodiscard]] u32 textureCount() const noexcept;

private:
    UiRenderer();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor::ui
