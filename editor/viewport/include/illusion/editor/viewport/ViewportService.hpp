#pragma once

/// @file
/// The GPU viewports of the editor process: one SceneRenderer per viewport rendering the edited World into a
/// texture the UI shows, the ground grid, DebugDraw visualisers, hover highlight and selection outline
/// overlays, icon billboards, the picture-in-picture view through a selected Camera, and asynchronous
/// picking. It is the ViewportHost of the `viewport.*` commands and the Scene panel.
///
/// ```cpp
/// auto viewports = ViewportService::create(device, editor, offscreen->renderer());
/// while (running)
/// {
///     // ... the shell draws the Scene panel (ViewportHost::frame()) ...
///     viewports->render(true);       // before the UI is rendered
///     // ... render and present the UI ...
/// }
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>

namespace illusion
{
class RenderDevice;
} // namespace illusion

namespace illusion::editor
{
class EditorContext;
} // namespace illusion::editor

namespace illusion::editor::ui
{
class UiRenderer;
} // namespace illusion::editor::ui

namespace illusion::editor::viewport
{

/// Statistics of the last ViewportService::render().
struct ViewportServiceStats
{
    u32 rendered = 0;         ///< Viewports rendered.
    u32 picksPending = 0;     ///< Picks waiting for the GPU.
    u32 readbacksPending = 0; ///< Readbacks (screenshots, samples, box selects) waiting for the GPU.
    u32 visualisers = 0;      ///< DebugDraw visualisers recorded, summed over the viewports.
    u32 previews = 0;         ///< Picture-in-picture camera views rendered (0 or 1).
};

/// The editor's viewports: "scene" (the Scene panel), "game" (the Game panel, through the scene's main
/// Camera) and up to three secondary Scene viewports. Use it on the main thread; the device, editor and UI
/// renderer outlive it. Registers itself with EditorContext::setViewports() and unregisters when destroyed.
class ViewportService final : public ViewportHost
{
public:
    /// Creates the service with the "scene" and "game" viewports. Returns nullptr (and logs why) if a GPU
    /// object cannot be created.
    [[nodiscard]] static std::unique_ptr<ViewportService> create(RenderDevice& device, EditorContext& editor,
                                                                 ui::UiRenderer& ui);
    ~ViewportService() override;
    ViewportService(const ViewportService&) = delete;            ///< Not copyable.
    ViewportService& operator=(const ViewportService&) = delete; ///< Not copyable.

    /// Brings render assets and derived Transforms up to date, renders the viewports the UI drew since the
    /// last call when @p uiShown is true (the UI is rendered this frame), and those with picks waiting in any
    /// case, then reads finished picks. Submits GPU work without waiting for it.
    void render(bool uiShown);

    /// Waits until the GPU has finished every pick and asset upload (for tests and headless tools).
    void waitIdle();

    /// Statistics of the last render().
    [[nodiscard]] const ViewportServiceStats& stats() const noexcept;

    /// The colour texture (RGBA8_UNORM, sRGB-encoded) the UI shows for @p id, for captures; null before the
    /// viewport has a size or for unknown ids. Replaced when the size changes.
    [[nodiscard]] nvrhi::ITexture* target(std::string_view id) const noexcept;

    // ViewportHost
    [[nodiscard]] std::vector<ViewportInfo> viewports() const override;
    [[nodiscard]] Expected<void, CommandError> setCamera(std::string_view id, const ViewportCamera& camera,
                                                         bool animate) override;
    [[nodiscard]] Expected<void, CommandError> setSettings(std::string_view id,
                                                           const ViewportSettings& settings) override;
    [[nodiscard]] Expected<ViewportCamera, CommandError> framing(std::string_view id,
                                                                 std::span<const Entity> entities) override;
    [[nodiscard]] Expected<u64, CommandError> requestPick(std::string_view id, Vec2 pixel,
                                                          bool cycle) override;
    [[nodiscard]] std::optional<Expected<std::optional<ViewportHit>, CommandError>> pick(u64 ticket) override;
    [[nodiscard]] ViewportFrame frame(std::string_view id, UVec2 size, const ViewportInput& input,
                                      const ViewportStyle& style) override;
    [[nodiscard]] Expected<std::string, CommandError> createViewport() override;
    [[nodiscard]] u64 viewportsVersion() const noexcept override;
    [[nodiscard]] Expected<void, CommandError> closeViewport(std::string_view id) override;
    [[nodiscard]] Expected<u64, CommandError> requestReadback(std::string_view id, ViewportChannel channel,
                                                              UVec2 origin, UVec2 extent) override;
    [[nodiscard]] std::optional<Expected<ViewportReadback, CommandError>> readback(u64 ticket) override;
    [[nodiscard]] Expected<ViewportRenderStats, CommandError> renderStats(std::string_view id) const override;
    [[nodiscard]] ViewportGpuInfo gpuInfo() const override;
    void meshBounds(std::vector<ViewportBounds>& out) override;

    /// The service's state, defined in its sources.
    struct Impl;

private:
    explicit ViewportService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor::viewport
