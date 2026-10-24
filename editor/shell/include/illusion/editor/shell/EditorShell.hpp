#pragma once
/// @file
/// The editor window's user interface: menu bar, toolbar, dock space with panels, status bar, toasts, the
/// command palette, console, history, AI Activity, preferences and the project windows. It draws the editor
/// state and changes it only through commands (UiActions), so people and agents share one API; it is also
/// the UiHost behind the `ui.*` commands.
///
/// ```cpp
/// EditorShell shell(editor, uiContext, {.editorData = &vfs, .fonts = fonts.get()});
/// shell.setAgents(&server);
/// while (!shell.quitRequested())
/// {
///     shell.beginFrame(contentScale);        // before ImGui::NewFrame()
///     ImGui::NewFrame();
///     shell.draw(now, deltaSeconds);
///     ImGui::Render();
///     // render, then answer shell.pendingScreenshot() with completeScreenshot()
/// }
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/UiHost.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class VirtualFileSystem;
} // namespace illusion

namespace illusion::editor
{
class EditorContext;
} // namespace illusion::editor

namespace illusion::editor::rpc
{
class RpcServer;
} // namespace illusion::editor::rpc

namespace illusion::editor::ui
{
class FontManager;
class FrameText;
class Theme;
class UiContext;
} // namespace illusion::editor::ui

namespace illusion::editor::shell
{

/// Settings of an EditorShell.
struct ShellOptions
{
    /// The editor data with "editor:" mounted (themes, locales, licences, templates). Must outlive the shell.
    const VirtualFileSystem* editorData = nullptr;
    /// Fonts of the UI context (the shell rescales them when the UI scale changes); may be null in tests.
    ui::FontManager* fonts = nullptr;
    /// Directory of the user's saved layouts (`layouts/*.ini`); empty keeps them in memory.
    std::filesystem::path userDirectory{};
    /// The process can capture frames (it has a GPU): `editor.screenshot` works.
    bool canCapture = false;
    /// Apply the agentPermissions preference to the agents' server (the window does; a headless editor keeps
    /// its --permissions).
    bool agentPermissions = true;
    /// Version text shown in About, e.g. "0.7.0".
    std::string version{};
};

/// A frame capture the shell asks the application for.
struct ScreenshotRequest
{
    u64 ticket = 0; ///< Pass to EditorShell::completeScreenshot().
    UVec2 size{0};  ///< Requested size (0, 0: the window's size).
};

/// The editor's user interface. Not thread-safe; use it on the UI thread with its UiContext current.
class EditorShell final : public UiHost
{
public:
    /// Builds the shell for @p editor in @p context and registers it as the editor's UiHost. Both must
    /// outlive it.
    EditorShell(EditorContext& editor, ui::UiContext& context, ShellOptions options);
    /// Saves the current arrangement for the next start and unregisters from the editor.
    ~EditorShell() override;

    EditorShell(const EditorShell&) = delete;            ///< Not copyable.
    EditorShell& operator=(const EditorShell&) = delete; ///< Not copyable.

    /// Shows the agents of @p server (may be null) in the AI Activity panel and its approval prompts. The
    /// server must outlive the shell or be reset to null first.
    void setAgents(rpc::RpcServer* server) noexcept;

    /// Applies changed preferences (theme, accent, scale, language, keymap) and pending layouts. Call before
    /// ImGui::NewFrame(). @p contentScale is the monitor's content scale.
    void beginFrame(f32 contentScale = 1.0f);

    /// Draws one frame at time @p now (seconds; drives toasts) after a frame of @p deltaSeconds. Call
    /// between ImGui::NewFrame() and ImGui::Render().
    void draw(f64 now, f64 deltaSeconds);

    /// True once Exit was chosen (`ui.quit`).
    [[nodiscard]] bool quitRequested() const noexcept;

    /// The oldest capture request that waits, if any (see completeScreenshot()).
    [[nodiscard]] std::optional<ScreenshotRequest> pendingScreenshot() const noexcept;
    /// Answers the request @p ticket with the frame rendered after it, or why it could not be captured.
    void completeScreenshot(u64 ticket, Expected<UiScreenshot, CommandError> result);

    /// The theme in use.
    [[nodiscard]] const ui::Theme& theme() const noexcept;
    /// The per-frame text of the current frame.
    [[nodiscard]] ui::FrameText& text() noexcept;
    /// The background colour behind the dock space (gamma-space RGBA), for clearing the UI target.
    [[nodiscard]] Vec4 clearColor() const noexcept;
    /// True while the command palette is open.
    [[nodiscard]] bool paletteOpen() const noexcept;
    /// The number of toasts shown.
    [[nodiscard]] usize toastCount() const noexcept;

    // UiHost
    [[nodiscard]] std::vector<UiPanelInfo> panels() const override;
    [[nodiscard]] Expected<void, CommandError> showPanel(std::string_view id, bool open) override;
    [[nodiscard]] std::vector<UiLayoutInfo> layouts() const override;
    [[nodiscard]] Expected<void, CommandError> applyLayout(std::string_view name) override;
    [[nodiscard]] Expected<void, CommandError> saveLayout(std::string_view name) override;
    [[nodiscard]] Expected<void, CommandError> deleteLayout(std::string_view name) override;
    void resetLayout() override;
    [[nodiscard]] Expected<void, CommandError> openPalette(std::string_view mode,
                                                           std::string_view query) override;
    void notify(UiNotice notice, std::string_view message) override;
    void requestQuit() override;
    [[nodiscard]] Expected<u64, CommandError> requestScreenshot(UVec2 size) override;
    [[nodiscard]] std::optional<Expected<UiScreenshot, CommandError>> screenshot(u64 ticket) override;

    /// The shell's state, defined in the library's sources (its panels use it).
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor::shell
