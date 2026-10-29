#pragma once
// The shell's panels and windows. Each draws into the current ImGui window (the shell begins and ends it)
// and changes editor state only through EditorShell::Impl::run() and runAction().

#include "ShellImpl.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>
#include <illusion/editor/ui/FontManager.hpp>

#include <array>
#include <imgui.h>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace illusion::editor::shell::detail
{

using Impl = EditorShell::Impl;

/// Pushes a font role of the shell's fonts (when it has fonts) for its lifetime.
class ScopedFont
{
public:
    /// Pushes @p role of @p fonts (nothing when @p fonts is null).
    ScopedFont(const ui::FontManager* fonts, ui::FontRole role) noexcept
        : m_pushed(fonts != nullptr)
    {
        if (fonts != nullptr)
        {
            fonts->push(role);
        }
    }

    /// Pops the font.
    ~ScopedFont()
    {
        if (m_pushed)
        {
            ImGui::PopFont();
        }
    }

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

private:
    bool m_pushed;
};

/// Sets up the columns of the current table as stretched columns with @p weights. Without it, proportional
/// stretching divides by the widths of empty columns in the first frame.
inline void setupStretchColumns(std::initializer_list<f32> weights)
{
    for (const f32 weight : weights)
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, weight);
    }
}

/// @p color as a Dear ImGui colour.
[[nodiscard]] inline ImVec4 imvec(Vec4 color) noexcept
{
    return ImVec4(color.x, color.y, color.z, color.w);
}

/// The command palette and Go to Anything: a search over commands, entities or preference pages, with a
/// form for the params of a command that needs them.
class Palette
{
public:
    Palette();
    ~Palette();
    Palette(const Palette&) = delete;
    Palette& operator=(const Palette&) = delete;

    /// Opens in @p mode ("commands", "entities" or "settings") with @p query typed.
    void open(std::string_view mode, std::string_view query);
    /// Opens the form of @p command with the fields of @p params (a JSON object) filled in.
    void openForm(Impl& shell, std::string_view command, std::string_view params, std::string_view origin);
    /// Closes it.
    void close() noexcept { m_open = false; }
    /// True while it is open.
    [[nodiscard]] bool isOpen() const noexcept { return m_open; }
    /// Draws it (when open) as a floating window at the top of the viewport.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
    bool m_open = false;
};

/// The Outliner panel: virtualized scene hierarchy tree with search filters, multi-selection, inline
/// rename, drag-and-drop reparenting and reordering, context menus and viewport visibility/lock toggles.
class Outliner
{
public:
    Outliner();
    ~Outliner();
    Outliner(const Outliner&) = delete;
    Outliner& operator=(const Outliner&) = delete;

    /// Starts inline renaming of @p entity in the next frame.
    void beginRename(EntityGuid entity) noexcept;
    /// Draws the panel's content.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Inspector panel: entity header, lock toggle, Local/World Transform card with Euler continuity cache,
/// metadata-driven and custom Component cards, multi-entity editing, inline validation and Add Component.
class Inspector
{
public:
    Inspector();
    ~Inspector();
    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    /// Draws the panel's content.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Console panel: log messages with level filters, search, collapsing, timestamps and source links,
/// and a command line.
class Console
{
public:
    explicit Console(usize capacity = 2048);
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    /// Copies new log messages (every frame, also while hidden).
    void update(Impl& shell);
    /// Draws the panel's content.
    void draw(Impl& shell);
    /// Number of stored messages.
    [[nodiscard]] usize size() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// A viewport panel (Scene, Game or a secondary Scene viewport): the viewport's image with navigation, click
/// and box picking, camera bookmarks and its toolbar.
class SceneView
{
public:
    /// A panel of the viewport @p id.
    explicit SceneView(std::string id);

    /// Draws the panel's content.
    void draw(Impl& shell);

    /// The viewport id.
    [[nodiscard]] const std::string& id() const noexcept { return m_id; }

private:
    void drawToolbar(Impl& shell);
    void drawAspectMenu(Impl& shell);
    void drawOverlaysMenu(Impl& shell);
    void handleBookmarks(Impl& shell, bool active);
    void run(Impl& shell, std::string_view command, std::string_view params);

    std::string m_id;
    ViewportFrame m_last;    // the viewport's state of the previous frame (for the menus)
    bool m_boxing = false;   // a box selection is being dragged
    bool m_cubeDrag = false; // a left drag that started on the view cube orbits
};

/// Assigns @p valueJson to the field @p path of the Component @p type of @p entities (one step; a batch for
/// several entities) as the Inspector does, with @p mergeKey for drags.
void setEntityField(Impl& shell, std::span<const EntityGuid> entities, std::string_view type,
                    std::string_view path, std::string_view valueJson, std::string_view mergeKey = {});

/// Starts the entity eyedropper for the field @p path (named @p field) of @p type on @p entities.
void startEyedropper(Impl& shell, std::span<const EntityGuid> entities, std::string_view type,
                     std::string_view path, std::string_view field);

/// Finishes the eyedropper's pending pick, or cancels the eyedropper on Escape (every frame).
void updateEyedropper(Impl& shell);

/// Frames an agent's new selection in the Scene view when Preferences::followAgent is on (every frame).
void followAgent(Impl& shell);

/// Draws a window for each secondary Scene viewport ("scene-2" ...); closing one closes the viewport.
void drawSecondaryViewports(Impl& shell);

/// The History panel: undo steps with origin badges, jumping, and undoing an agent's changes.
class History
{
public:
    /// Draws the panel's content.
    void draw(Impl& shell);
};

/// The AI Activity panel: connected agents, approvals, the call feed, pausing and disconnecting.
class Agents
{
public:
    Agents();
    ~Agents();
    Agents(const Agents&) = delete;
    Agents& operator=(const Agents&) = delete;

    /// Refreshes the copies of the server's state when it changed and announces new approvals.
    void update(Impl& shell);
    /// Draws the panel's content.
    void draw(Impl& shell);
    /// Draws the approval pop-up for the oldest waiting request (when there is one).
    void drawPrompt(Impl& shell);
    /// Number of connected agents.
    [[nodiscard]] usize clientCount() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The pages of the Preferences window.
enum class PreferencesTab : u8
{
    Appearance,
    Language,
    Keyboard,
    Agents,
    Autosave,
    ExternalTools,
    Count,
};

/// The Preferences window's content.
class PreferencesPage
{
public:
    PreferencesPage();
    ~PreferencesPage();
    PreferencesPage(const PreferencesPage&) = delete;
    PreferencesPage& operator=(const PreferencesPage&) = delete;

    /// Shows @p tab.
    void select(PreferencesTab tab) noexcept;
    /// Draws the window's content.
    void draw(Impl& shell);
    /// True while a new shortcut is being captured (keys go to the capture, not to shortcuts).
    [[nodiscard]] bool capturing() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The title key of a preferences tab.
[[nodiscard]] text::TextKey preferencesTabTitle(PreferencesTab tab) noexcept;

/// The Welcome window (recent projects, new project, the folder browser), About and the licence viewer.
class Projects
{
public:
    Projects();
    ~Projects();
    Projects(const Projects&) = delete;
    Projects& operator=(const Projects&) = delete;

    /// Draws the Welcome window's content.
    void drawWelcome(Impl& shell);
    /// Draws the About window's content.
    void drawAbout(Impl& shell);
    /// Draws the licence viewer's content.
    void drawLicenses(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The workspace-relative form of the absolute project directory @p path ("" if it is outside).
[[nodiscard]] std::string workspaceRelative(const EditorContext& editor, std::string_view path);

/// @p text quoted as a JSON string.
[[nodiscard]] std::string jsonQuote(std::string_view text);

} // namespace illusion::editor::shell::detail
