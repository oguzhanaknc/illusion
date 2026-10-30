#pragma once
// The shell's panels and windows. Each draws into the current ImGui window (the shell begins and ends it)
// and changes editor state only through EditorShell::Impl::run() and runAction().

#include "ShellImpl.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/editor/core/Gizmo.hpp>
#include <illusion/editor/core/Snap.hpp>
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

/// Keeps one snap request of a viewport in flight while the user snaps, and remembers the latest answer.
class SnapTracker
{
public:
    /// Collects the answer in flight and asks @p host for @p target at @p pixel of @p id once it is answered
    /// (again only when the pixel or the target changes).
    void update(ViewportHost& host, std::string_view id, Vec2 pixel, ViewportSnapTarget target, f32 tolerance,
                std::span<const Entity> exclude);

    /// Forgets the answer; the request in flight is collected and dropped by the next update().
    void reset() noexcept;

    /// The latest answer: the point, or std::nullopt before one or when nothing was found.
    [[nodiscard]] const std::optional<ViewportSnap>& found() const noexcept { return m_found; }

    /// A request since the last reset() was answered.
    [[nodiscard]] bool answered() const noexcept { return m_any; }

private:
    u64 m_ticket = 0;
    bool m_drop = false;     // the answer in flight belongs to an earlier request
    bool m_answered = false; // m_found answers m_pixel and m_target
    bool m_any = false;
    Vec2 m_pixel{-1.0f};
    ViewportSnapTarget m_target = ViewportSnapTarget::Any;
    std::optional<ViewportSnap> m_found;
};

/// The transform gizmo of a Scene view: it draws the gizmo of the selection, finds the handle under the
/// cursor and runs drags as `transform.set` calls inside one transaction (Esc or the right button cancels).
class ViewportGizmo
{
public:
    /// What the panel tells the gizmo each frame.
    struct Frame
    {
        const ViewportFrame* viewport = nullptr; ///< The viewport's frame (Scene viewports only).
        Vec2 imageOrigin{0.0f};                  ///< Top left of the image in the window.
        f32 toPanel = 1.0f;                      ///< Window points per image pixel.
        f32 pixelScale = 1.0f;                   ///< Image pixels per display point.
        Vec2 cursor{0.0f};                       ///< Cursor in image pixels.
        bool hovered = false;                    ///< The cursor is over the image and no overlay.
        std::string_view id;                     ///< The viewport id.
    };

    /// The handle under the cursor of @p frame (None while the gizmo is hidden or Alt is held).
    [[nodiscard]] GizmoHandle hoveredHandle(Impl& shell, const Frame& frame);

    /// Starts dragging @p handle at the cursor of @p frame. Returns false when it cannot start (another
    /// caller holds a transaction).
    bool begin(Impl& shell, const Frame& frame, GizmoHandle handle);

    /// Starts a modal @p operation of the selection at the cursor of @p frame. Returns false when nothing can
    /// move or another caller holds a transaction.
    bool beginModal(Impl& shell, const Frame& frame, GizmoModalOperation operation);

    /// Applies the running drag or modal transform (a drag commits on release, a modal transform on Enter or
    /// a click; both commit on Enter and cancel on Esc or the right button; typed numbers replace the
    /// cursor's) and draws the gizmo over the image with @p hovered highlighted.
    void update(Impl& shell, const Frame& frame, ImDrawList& draw, GizmoHandle hovered);

    /// A drag or a modal transform is running.
    [[nodiscard]] bool dragging() const noexcept { return m_drag.has_value() || m_modal.has_value(); }

    /// A modal transform is running.
    [[nodiscard]] bool modal() const noexcept { return m_modal.has_value(); }

    /// Starts measuring with the Measure tool at the cursor of @p frame (the left button is held). Returns
    /// false when the tool is not Measure.
    bool beginMeasure(Impl& shell, const Frame& frame);

    /// The Measure tool is drawing a ruler.
    [[nodiscard]] bool measuring() const noexcept { return m_measuring; }

private:
    [[nodiscard]] std::optional<GizmoPose> pose(Impl& shell, const Frame& frame);
    void refreshTargets(Impl& shell);
    bool beginTransaction(Impl& shell, text::TextKey label);
    void apply(Impl& shell, const GizmoPose& pose, const GizmoDelta& delta);
    void end(Impl& shell, bool commit);
    void typeNumbers();
    void drawModal(Impl& shell, const Frame& frame, ImDrawList& draw);
    // Snaps the translation of the running drag to a vertex (V) or a surface (Ctrl+Shift); false when it does
    // not snap.
    bool snapDelta(Impl& shell, const Frame& frame, GizmoDelta& delta);
    void updateMeasure(Impl& shell, const Frame& frame, ImDrawList& draw);
    void drawSnapMarker(Impl& shell, const Frame& frame, ImDrawList& draw);

    std::vector<GizmoTarget> m_targets;
    std::vector<Transform> m_start;
    std::vector<Transform> m_placed;
    std::string m_params;
    u64 m_selectionVersion = 0;
    u64 m_revision = 0;
    bool m_cached = false;
    std::optional<GizmoDrag> m_drag;
    std::optional<GizmoModal> m_modal;
    GizmoTypedInput m_typed;
    GizmoTool m_dragTool = GizmoTool::Translate;
    GizmoPivot m_dragPivot = GizmoPivot::Median;
    GizmoDelta m_delta;
    // Snapping drags: the moved entities (left out of the search) and the point under the cursor.
    std::vector<Entity> m_excluded;
    SnapTracker m_snap;
    bool m_snapped = false;
    // The Measure tool: the points under the press and under the cursor.
    bool m_measuring = false;
    SnapTracker m_measureFrom;
    SnapTracker m_measureTo;
    Vec2 m_measurePress{0.0f};
};

/// Draws @p ruler over the image of @p frame with its length, offsets and elevation.
void drawRuler(Impl& shell, const ViewportGizmo::Frame& frame, ImDrawList& draw, const GizmoRuler& ruler);

/// The gizmo controls of the main toolbar after the tool buttons: axes, pivot and snapping.
void drawGizmoToolbar(Impl& shell);

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
    ViewportFrame m_last;        // the viewport's state of the previous frame (for the menus)
    bool m_boxing = false;       // a box selection is being dragged
    bool m_cubeDrag = false;     // a left drag that started on the view cube orbits
    bool m_gizmoPressed = false; // the left press went to the gizmo (no pick or box on release)
    ViewportGizmo m_gizmo;
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
