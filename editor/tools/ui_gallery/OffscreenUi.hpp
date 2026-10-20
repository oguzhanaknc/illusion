#pragma once

// Renders Dear ImGui draw data into an offscreen UI target and reads the pixels back, for gallery
// screenshots, golden-image tests and the sRGB/UNORM present comparison.

#include <illusion/assets/Image.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>
#include <optional>

struct ImDrawData;

namespace illusion
{
class RenderDevice;
} // namespace illusion

namespace illusion::editor::ui
{
class UiContext;
class UiRenderer;
} // namespace illusion::editor::ui

namespace illusion::editor::gallery
{

/// An RGBA8_UNORM UI target of a fixed size with a UiRenderer drawing into it.
class OffscreenUi : NonCopyable
{
public:
    /// Creates the target and the renderer for @p context. Returns nullptr if a GPU object cannot be created.
    [[nodiscard]] static std::unique_ptr<OffscreenUi> create(RenderDevice& device, ui::UiContext& context,
                                                             UVec2 size);

    ~OffscreenUi();

    /// Clears the target to @p clear (gamma-space colour), draws @p drawData and returns the target's pixels
    /// (sRGB-encoded RGBA8, as the UI is drawn in gamma space).
    [[nodiscard]] std::optional<Image> render(ImDrawData* drawData, Vec4 clear);

    /// Like render(), then copies the target with UiRenderer::present() into an output texture of
    /// @p outputFormat (RGBA8_UNORM or SRGBA8_UNORM) and returns the output's stored bytes.
    [[nodiscard]] std::optional<Image> renderPresented(ImDrawData* drawData, Vec4 clear,
                                                       nvrhi::Format outputFormat);

    /// The renderer.
    [[nodiscard]] ui::UiRenderer& renderer() noexcept { return *m_renderer; }

    /// The target size in pixels.
    [[nodiscard]] UVec2 size() const noexcept { return m_size; }

private:
    OffscreenUi() = default;

    void draw(nvrhi::ICommandList* commands, ImDrawData* drawData, Vec4 clear);
    std::optional<Image> readBack(nvrhi::ICommandList* commands, nvrhi::ITexture* texture);

    RenderDevice* m_device = nullptr;
    UVec2 m_size{0};
    std::unique_ptr<ui::UiRenderer> m_renderer;
    nvrhi::TextureHandle m_target;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::CommandListHandle m_commands;
};

} // namespace illusion::editor::gallery
