#pragma once
// Internals of EditorShell shared by its panels: the panel table, the shell state and the helpers panels use
// to run commands and show results.

#include <illusion/core/Expected.hpp>
#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/UiActions.hpp>
#include <illusion/editor/shell/EditorShell.hpp>
#include <illusion/editor/ui/FrameText.hpp>
#include <illusion/editor/ui/Icon.hpp>
#include <illusion/editor/ui/Theme.hpp>
#include <illusion/editor/ui/Widgets.hpp>
#include <illusion/text/StringTable.hpp>

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::shell::detail
{

/// Every panel and window of the shell.
enum class Panel : u8
{
    Outliner,
    Inspector,
    Scene,
    Game,
    Assets,
    Console,
    History,
    AiActivity,
    Profiler,
    Rendering,
    ProjectSettings,
    Preferences,
    Welcome,
    About,
    Licenses,
    Notifications,
    Count,
};

inline constexpr usize kPanelCount = static_cast<usize>(Panel::Count);

/// Static facts of a panel.
struct PanelInfo
{
    std::string_view id;          // "panel.console"
    text::TextKey title;          // localized title
    std::string_view icon;        // Material Symbols name
    bool docked;                  // part of the dock layout
    std::string_view milestone{}; // placeholder until this milestone ("" for a real panel)
};

/// The facts of @p panel.
[[nodiscard]] const PanelInfo& panelInfo(Panel panel) noexcept;

/// The panel with @p id.
[[nodiscard]] std::optional<Panel> findPanel(std::string_view id) noexcept;

/// The "###id" window name of @p panel (stable in every language).
[[nodiscard]] const char* windowId(Panel panel) noexcept;

/// The built-in layouts, in menu order.
inline constexpr std::array<std::string_view, 5> kBuiltInLayouts = {"default", "level-design", "debug",
                                                                    "profiling", "agent"};

/// The title key of the built-in layout @p name (table nullptr for saved layouts).
[[nodiscard]] text::TextKey builtInLayoutTitle(std::string_view name) noexcept;

/// The panels a built-in layout shows (the others are closed).
[[nodiscard]] std::array<bool, kPanelCount> builtInLayoutPanels(std::string_view name) noexcept;

/// Builds the dock nodes of the built-in layout @p name in the dock space @p dockspace of @p size.
void buildLayout(std::string_view name, u32 dockspace, Vec2 size);

/// Action ids of toasts.
inline constexpr u32 kToastShowAgents = 1;

/// One entry of the notification centre.
struct Notice
{
    UiNotice level = UiNotice::Info;
    f64 time = 0.0;
    std::array<char, 256> text{};
};

class Palette;
class Outliner;
class Inspector;
class SceneView;
class Console;
class History;
class Agents;
class PreferencesPage;
class Projects;

} // namespace illusion::editor::shell::detail

namespace illusion::editor::shell
{

/// The state of the shell. Panels get it to draw and to run commands.
struct EditorShell::Impl
{
    Impl(EditorShell& owner, EditorContext& editorContext, ui::UiContext& uiContext,
         ShellOptions shellOptions);
    ~Impl();

    EditorShell& shell;
    EditorContext& editor;
    ui::UiContext& context;
    ShellOptions options;
    rpc::RpcServer* agents = nullptr;

    ui::Theme baseTheme; // the theme file
    ui::Theme theme;     // with the accent applied
    ui::FrameText text;
    ui::ToastQueue toasts;
    Keymap keymap;
    f32 scale = 1.0f; // UI scale times content scale
    f32 contentScale = 1.0f;
    u64 preferencesVersion = 0; // applied preferences
    f64 now = 0.0;
    f64 frameMs = 0.0;
    bool quit = false;

    // Panels: open state, focus requests.
    std::array<bool, detail::kPanelCount> open{};
    std::array<bool, detail::kPanelCount> focus{};

    // Layouts: the active one, saved ones (name -> ini text), and what beginFrame()/draw() apply next.
    std::string activeLayout = "default";
    std::map<std::string, std::string, std::less<>> savedLayouts;
    std::optional<std::string> pendingIni;   // loaded before the next NewFrame()
    std::optional<std::string> pendingBuild; // built-in layout built in the next draw()
    u32 settleFrames = 0;                    // frames after a layout was built before focus requests apply
    bool userArrangement = false;            // the user's arrangement of the last session was restored
    const Project* lastProject = nullptr;    // the project whose shipped layout was considered

    // The notification centre (newest last).
    std::deque<detail::Notice> notices;
    static constexpr usize kMaxNotices = 64;

    // Screenshots: requested, then completed by the application.
    struct Capture
    {
        u64 ticket = 0;
        UVec2 size{0};
        std::optional<Expected<UiScreenshot, CommandError>> result;
    };
    std::vector<Capture> captures;
    u64 nextTicket = 1;

    // Async commands started from the UI, advanced every frame.
    std::vector<std::unique_ptr<RunningCommand>> running;

    std::unique_ptr<detail::Palette> palette;
    std::unique_ptr<detail::Outliner> outliner;
    std::unique_ptr<detail::Inspector> inspector;
    std::unique_ptr<detail::SceneView> sceneView;
    bool viewportNavigating =
        false; // the Scene panel moved its camera last frame (fly keys are not shortcuts)
    std::unique_ptr<detail::Console> console;
    std::unique_ptr<detail::History> history;
    std::unique_ptr<detail::Agents> agentsPanel;
    std::unique_ptr<detail::PreferencesPage> preferences;
    std::unique_ptr<detail::Projects> projects;

    /// The widget kit of this frame.
    [[nodiscard]] ui::UiKit kit();

    /// Runs @p command with @p params (a JSON object, "" for {}) as @p origin. Async commands keep running in
    /// the background. Failures are shown as a toast; the result JSON is returned on success.
    std::optional<std::string> run(std::string_view command, std::string_view params, std::string_view origin,
                                   std::string_view asOrigin = {}, std::string_view mergeKey = {});

    /// Runs the UI action @p index of uiActions(): its command with its params, or the palette's form when
    /// the command needs params the action does not give.
    void runAction(usize index, std::string_view origin);

    /// The title of the action @p index (the command's title by default).
    [[nodiscard]] text::TextKey actionTitle(usize index) const;

    /// The icon of the action @p index.
    [[nodiscard]] ui::Icon actionIcon(usize index) const;

    /// True when the action @p index can run (its command exists).
    [[nodiscard]] bool actionEnabled(usize index) const;

    /// Shows a toast and keeps it in the notification centre.
    void toast(UiNotice level, std::string_view message, std::string_view action = {}, u32 actionId = 0);

    /// Shows a failed command as a toast.
    void failed(std::string_view command, const CommandError& error);

    /// Opens (and focuses) or closes a panel directly (the commands call this).
    void setOpen(detail::Panel panel, bool value);

    /// Changes preferences with a merge patch (a JSON object) through `preferences.set`.
    void setPreferences(std::string_view patch);

    /// Reads the saved layouts of the user directory.
    void loadSavedLayouts();

    /// The ini text of the current arrangement, with the open panels.
    [[nodiscard]] std::string currentIni() const;

    /// Applies preferences that changed since the last frame.
    void applyPreferences();
};

} // namespace illusion::editor::shell
