#pragma once
/// @file
/// Runs Dear ImGui frames without a GPU: claims the renderer's texture support and answers the font atlas's
/// texture requests as a renderer would, so CPU-only tests, benchmarks and the editor without a GPU exercise
/// the same code paths (dynamic fonts included) as the windowed editor.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion::editor::ui
{

class UiContext;

/// A renderer stand-in for one UiContext.
class HeadlessUi : NonCopyable
{
public:
    /// Registers as the renderer of @p context (which must outlive this object).
    explicit HeadlessUi(UiContext& context);

    /// Releases the textures and unregisters.
    ~HeadlessUi();

    /// Sets the display size and frame time and calls ImGui::NewFrame().
    void beginFrame(Vec2 displaySize, f32 deltaSeconds = 1.0f / 60.0f);

    /// Calls ImGui::Render() and completes the texture requests of the frame.
    void endFrame();

    /// Number of textures created and not yet destroyed.
    [[nodiscard]] u32 textureCount() const noexcept { return m_textures; }

private:
    UiContext& m_context;
    u64 m_nextId = 1;
    u32 m_textures = 0;
};

} // namespace illusion::editor::ui
