#pragma once

// The UI gallery's pages: a widget catalogue, a typography and icon sheet, and an editor-style layout with
// sample data. Shared by the illusion_ui_gallery app, the editor UI tests and the gallery benchmark.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/ui/FrameText.hpp>
#include <illusion/editor/ui/Theme.hpp>
#include <illusion/editor/ui/Widgets.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace illusion
{
class VirtualFileSystem;
} // namespace illusion

namespace illusion::text
{
class Localization;
} // namespace illusion::text

namespace illusion::editor::ui
{
class FontManager;
} // namespace illusion::editor::ui

namespace illusion::editor::gallery
{

/// The pages of the gallery.
enum class GalleryPage : u8
{
    Widgets,
    Typography,
    Editor,
};

/// Draws the gallery into the main viewport. Not thread-safe; use it on the UI thread.
class Gallery : NonCopyable
{
public:
    /// Uses @p localization for text and @p fonts (may be null) for font roles; both must outlive the
    /// gallery. Registers the editor string table with @p localization.
    Gallery(text::Localization& localization, const ui::Theme& theme, const ui::FontManager* fonts);

    /// Switches to @p theme at @p scale (UI scale times content scale) and rebuilds the current context's
    /// style.
    void setTheme(const ui::Theme& theme, f32 scale);

    /// Shows @p page from the next draw() on.
    void setPage(GalleryPage page) noexcept;

    /// The page shown.
    [[nodiscard]] GalleryPage page() const noexcept { return m_page; }

    /// Draws one frame at time @p now (seconds; drives toasts). Call between ImGui::NewFrame() and
    /// ImGui::Render().
    void draw(f64 now);

    /// Shows the sample toast ("Deleted 12 entities. Undo") at time @p now.
    void showToast(f64 now);

    /// Number of times the toast's Undo action was clicked.
    [[nodiscard]] u32 undoCount() const noexcept { return m_undoCount; }

    /// The per-frame text (reset by draw()).
    [[nodiscard]] ui::FrameText& text() noexcept { return m_text; }

    /// The state of the widgets, for tests.
    struct State
    {
        int tool = 0;                          ///< Selected transform tool.
        bool snap = true;                      ///< "Snap to grid".
        bool gizmos = false;                   ///< "Show gizmos".
        int space = 0;                         ///< Gizmo space.
        int pivot = 0;                         ///< Gizmo pivot.
        std::array<char, 64> search{};         ///< Search field text.
        std::array<char, 64> outlinerSearch{}; ///< Outliner search text.
        std::array<char, 64> name{};           ///< Entity name.
        Vec3 position{1.25f, 0.0f, -3.5f};     ///< Transform position.
        Vec3 rotation{0.0f, 45.0f, 0.0f};      ///< Transform rotation (degrees).
        Vec3 scale{1.0f};                      ///< Transform scale.
        bool meshEnabled = true;               ///< Mesh renderer enabled.
        bool castShadows = true;               ///< Mesh renderer casts shadows.
        bool lightEnabled = true;              ///< Light enabled.
        Vec3 lightColor{1.0f, 0.78f, 0.55f};   ///< Light colour.
        f32 intensity = 800.0f;                ///< Light intensity (cd).
        f32 range = 12.0f;                     ///< Light range (m).
        int selectedTile = 0;                  ///< Selected asset tile.
        int selectedEntity = 3;                ///< Selected outliner row.
        int clicks = 0;                        ///< Clicks of the primary button.
    };

    /// The widgets' state.
    [[nodiscard]] State& state() noexcept { return m_state; }

private:
    ui::UiKit kit();
    void drawWidgets(ui::UiKit& kit, f64 now);
    void drawTypography(ui::UiKit& kit);
    void drawEditor(ui::UiKit& kit);
    void drawTransformCard(ui::UiKit& kit, const char* id);
    void drawMeshCard(ui::UiKit& kit, const char* id);
    void drawLightCard(ui::UiKit& kit, const char* id);
    void drawToolButtons(ui::UiKit& kit, const char* id);

    text::Localization& m_localization;
    const ui::Theme* m_theme;
    const ui::FontManager* m_fonts;
    ui::FrameText m_text;
    ui::ToastQueue m_toasts;
    State m_state;
    GalleryPage m_page = GalleryPage::Widgets;
    bool m_pageChanged = true;
    f32 m_scale = 1.0f;
    u32 m_undoCount = 0;
    std::array<f32, 120> m_frameTimes{};
    std::array<f32, 120> m_frameIndices{};
};

} // namespace illusion::editor::gallery
