#pragma once
// The shell's panels and windows. Each draws into the current ImGui window (the shell begins and ends it)
// and changes editor state only through EditorShell::Impl::run() and runAction().

#include "ShellImpl.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/editor/core/Gizmo.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/editor/core/Snap.hpp>
#include <illusion/editor/core/UiPlacementEdit.hpp>
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

/// The live part of the Animator's Inspector card (ADR 0059-0061): the clip or graph, a timeline scrubber
/// over Animator::time (edit World, through `animation.scrub`), the state of each layer, the parameters (set
/// in Play mode through `animation.set_parameter` and `animation.trigger`) and the last events of the running
/// game. It reads the edit World's poses from the viewports (ViewportHost::editAnimation()) and the running
/// game's from the PlayHost, and draws only a hint without them. Idle frames do not allocate.
class AnimatorSection
{
public:
    AnimatorSection();
    ~AnimatorSection();
    AnimatorSection(const AnimatorSection&) = delete;
    AnimatorSection& operator=(const AnimatorSection&) = delete;

    /// Draws the section of the Animator of @p entity (the edit World's Entity with @p guid) below the card
    /// @p cardId; the undo steps of one scrub merge under @p mergeKey.
    void draw(Impl& shell, ui::UiKit& kit, Entity entity, EntityGuid guid, std::string_view cardId,
              std::string_view mergeKey);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Inspector panel: entity header, lock toggle, Local/World Transform card with Euler continuity cache,
/// metadata-driven and custom Component cards, multi-entity editing, inline validation and Add Component.
class Inspector
{
public:
    /// An Inspector whose stable ids start with @p idPrefix (a static string, e.g. "inspector" for the
    /// Inspector panel or "inspector_2" for the second Inspector window).
    explicit Inspector(std::string_view idPrefix = "inspector");
    ~Inspector();
    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    /// Draws the panel's content.
    void draw(Impl& shell);

    /// Shows @p entities from now on whatever the selection (the lock button), or the selection again when
    /// @p entities is empty.
    void lock(std::span<const EntityGuid> entities);

    /// True while it shows locked entities instead of the selection.
    [[nodiscard]] bool locked() const noexcept;

    /// Draws the fields of the project settings value @p work of @p type with the Inspector's widgets, except
    /// the fields named in @p skip (drawn by the caller). Edits update @p work and run `project.settings_set`
    /// with the changed top-level field; resetting a field takes its value from @p defaults.
    void drawSettings(Impl& shell, const TypeReflection& type, void* work, const void* defaults,
                      std::span<const std::string_view> skip = {});

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// Draws Inspector window @p index (0 for "Inspector 2"): it locks itself to the selection when it opens.
void drawExtraInspector(Impl& shell, usize index);

/// Runs `project.settings_set` for @p type with the top-level field @p field of @p work as the merge patch
/// (merging undo steps with @p mergeKey). False if the field cannot be written as JSON.
bool writeSettingsField(Impl& shell, const TypeReflection& type, const void* work, std::string_view field,
                        std::string_view mergeKey = {});

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

/// Canvas editing in the Game view while the game does not run (ADR 0057, UI-3d): picks game UI elements,
/// draws the outlines, handles and anchor markers of the selection over the image, and runs handle drags as
/// `gameui.place` and `gameui.set_anchors` calls inside one transaction (Esc or the right button cancels).
class CanvasEditor
{
public:
    /// What the Game view tells the editor each frame.
    struct Frame
    {
        std::string_view id;    ///< The viewport id.
        bool active = false;    ///< Canvas editing applies: a Game view with an image, the game not running.
        Vec2 imageOrigin{0.0f}; ///< Top left of the image in the window.
        Vec2 display{0.0f};     ///< Size of the image in the window.
        f32 toPanel = 1.0f;     ///< Window points per image pixel.
        UVec2 size{0};          ///< The image in pixels: the UI target.
        Vec2 cursor{0.0f};      ///< Cursor in image pixels.
        bool hovered = false;   ///< The cursor is over the image.
    };

    /// Handles a press on the image: true when it grabbed a handle of the primary selection (the view then
    /// draws no selection box; a release without a drag still picks).
    bool press(Impl& shell, const Frame& frame);

    /// Refreshes the layout when needed, applies the running drag (it starts once the pressed handle moves
    /// past the drag threshold, commits on release) and draws the overlays.
    void update(Impl& shell, const Frame& frame, ImDrawList& draw);

    /// The game UI element under the cursor of the last update() (null for none).
    [[nodiscard]] Entity hovered() const noexcept { return m_hovered; }

    /// A handle is pressed or dragged.
    [[nodiscard]] bool pressed() const noexcept { return m_pressed != UiHandle::None; }

    /// A drag is running.
    [[nodiscard]] bool dragging() const noexcept { return m_drag.handle() != UiHandle::None; }

    /// The last press became a drag (or was cancelled): its release does not pick.
    [[nodiscard]] bool consumed() const noexcept { return m_consumed; }

private:
    // The primary selection's frame and its canvas's pixels per unit, from the cached layout.
    struct Primary
    {
        UiHandleFrame frame{};
        f32 pixelsPerUnit = 1.0f;
        EntityGuid guid{};
        bool valid = false;
    };

    bool refresh(Impl& shell, const Frame& frame);
    void updatePrimary(Impl& shell);
    Entity pick(Vec2 cursor);
    [[nodiscard]] UiHandle handleAt(const Frame& frame, Vec2 cursor, f32 scale) const;
    void begin(Impl& shell, const Frame& frame);
    void apply(Impl& shell, const UiHandleEdit& edit);
    void end(Impl& shell, bool commit);
    void drawOverlays(Impl& shell, const Frame& frame, ImDrawList& draw, UiHandle hot);

    UiLayout m_layout;  // what the Game view drew last
    UiLayout m_scratch; // one canvas of m_layout, for picking
    bool m_hasLayout = false;
    Primary m_primary;
    Entity m_hovered{};
    UiHandle m_pressed = UiHandle::None; // the handle under the press, until release
    Vec2 m_pressCursor{0.0f};            // the press in image pixels
    UiHandleDrag m_drag;
    Primary m_dragged;        // the primary selection when the drag started
    UiHandleEdit m_applied{}; // the edit the last command wrote
    UVec2 m_target{0};        // the UI target of the running drag
    bool m_consumed = false;
    std::string m_params;
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
    bool handleGameInput(Impl& shell, ImDrawList& draw, Vec2 imageOrigin, Vec2 display, Vec2 toImage,
                         bool clicked);
    void run(Impl& shell, std::string_view command, std::string_view params);

    std::string m_id;
    ViewportFrame m_last;        // the viewport's state of the previous frame (for the menus)
    bool m_boxing = false;       // a box selection is being dragged
    bool m_cubeDrag = false;     // a left drag that started on the view cube orbits
    bool m_gizmoPressed = false; // the left press went to the gizmo (no pick or box on release)
    ViewportGizmo m_gizmo;
    CanvasEditor m_canvas; // game UI editing (Game views only)
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

/// Routes the window's keyboard and mouse to the running game while the Game view has captured them (a
/// click on the Game view while playing). Shift+F1, Stop and Pause let go: the game then sees every held key
/// and button released. Editor shortcuts wait while the game has the input.
class GameInput
{
public:
    /// Gives the game the keyboard, the mouse and the cursor (hidden and unbounded) from the next update().
    void capture() noexcept;

    /// Takes the input back from the game and releases what it sees held.
    void release(Impl& shell);

    /// True while the game has the input.
    [[nodiscard]] bool captured() const noexcept { return m_captured; }

    /// Where the Game view draws its image this frame: cursor positions sent to the game are
    /// `(mouse - origin) * toImage` (pixels of the game view).
    void setImage(Vec2 origin, Vec2 toImage) noexcept;

    /// Every frame before the panels draw: lets go on Shift+F1 or when no session plays, else sends the game
    /// the keys, buttons and cursor position that changed since the last frame.
    void update(Impl& shell);

private:
    std::array<bool, static_cast<usize>(Key::Count)> m_keys{};
    std::array<bool, static_cast<usize>(MouseButton::Count)> m_buttons{};
    Vec2 m_cursor{-1.0f};
    Vec2 m_origin{0.0f};
    Vec2 m_toImage{1.0f};
    bool m_captured = false;
};

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

/// The Profiler panel: the editor's frame times and the time per place of the frame, the GPU passes of each
/// viewport, and the problems `diagnostics.get` finds.
class ProfilerPanel
{
public:
    ProfilerPanel();
    ~ProfilerPanel();
    ProfilerPanel(const ProfilerPanel&) = delete;
    ProfilerPanel& operator=(const ProfilerPanel&) = delete;

    /// Draws the panel's content.
    void draw(Impl& shell);

    /// What the panel keeps between frames (defined in ProfilerPanel.cpp).
    struct State;

private:
    std::unique_ptr<State> m_state;
};

/// Draws the Physics window: the physics of the running play session and a button that shows collision
/// shapes.
void drawPhysicsWindow(Impl& shell);

/// Draws the Audio Mixer window: the mixer and voices of the running play session.
void drawAudioMixerWindow(Impl& shell);

/// The Input Actions window: the actions of an input actions asset, rebound through the `input.*` commands.
class InputActionsWindow
{
public:
    InputActionsWindow();
    ~InputActionsWindow();
    InputActionsWindow(const InputActionsWindow&) = delete;
    InputActionsWindow& operator=(const InputActionsWindow&) = delete;

    /// Draws the window's content.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Gamepad Tester window: the live state of a connected gamepad and its outputs (`gamepad.*` commands).
class GamepadTesterWindow
{
public:
    GamepadTesterWindow();
    ~GamepadTesterWindow();
    GamepadTesterWindow(const GamepadTesterWindow&) = delete;
    GamepadTesterWindow& operator=(const GamepadTesterWindow&) = delete;

    /// Draws the window's content.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Export window: the export settings (`project.settings_set`), the target, and `export.build` with its
/// progress, result and diagnostics.
class ExportWindow
{
public:
    ExportWindow();
    ~ExportWindow();
    ExportWindow(const ExportWindow&) = delete;
    ExportWindow& operator=(const ExportWindow&) = delete;

    /// Advances the running export (every frame, also while the window is closed).
    void update(Impl& shell);
    /// Draws the window's content.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Rendering panel: the render quality of the viewports (a preference), the GPU and each viewport's last
/// frame.
class RenderingPanel
{
public:
    /// Draws the panel's content.
    void draw(Impl& shell);

private:
    std::vector<std::string> m_viewports; // ids, read again when the host's list changes
    u64 m_viewportsVersion = ~u64{0};
};

/// The Project Settings panel: a page per project settings type, edited through `project.settings_set`.
class ProjectSettingsPanel
{
public:
    ProjectSettingsPanel();
    ~ProjectSettingsPanel();
    ProjectSettingsPanel(const ProjectSettingsPanel&) = delete;
    ProjectSettingsPanel& operator=(const ProjectSettingsPanel&) = delete;

    /// Draws the panel's content.
    void draw(Impl& shell);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The Asset Browser panel: the project's `assets:` mount as a navigable grid and list with thumbnails, a
/// folder tree and favourites, search, sorting and a kind filter; file operations run the asset commands,
/// and the import settings of a selected texture or model are edited inline. A project with game modules
/// also shows its `modules:` mount as a second root, to browse and to open sources in the code editor (the
/// asset-only features are off there).
class AssetBrowser
{
public:
    AssetBrowser();
    ~AssetBrowser();
    AssetBrowser(const AssetBrowser&) = delete;
    AssetBrowser& operator=(const AssetBrowser&) = delete;

    /// Draws the panel's content.
    void draw(Impl& shell);

    /// Navigates to the folder of @p path and selects it (the palette's "#" search).
    void reveal(std::string_view path);

    /// The folder of `assets:` that OS file drops import into, e.g. "assets:/models": the folder shown now,
    /// or the assets root while a `modules:` folder is shown.
    [[nodiscard]] std::string_view currentFolder() const noexcept;

    /// Lists the `modules:` mount again at the next draw (a module command ran).
    void markModulesStale() noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The workspace-relative form of the absolute project directory @p path ("" if it is outside).
[[nodiscard]] std::string workspaceRelative(const EditorContext& editor, std::string_view path);

/// @p text quoted as a JSON string.
[[nodiscard]] std::string jsonQuote(std::string_view text);

/// Drag-and-drop payload type of entities: the 16 hex digits of the EntityGuid (Outliner rows).
inline constexpr const char* kEntityDragPayload = "ILLUSION_ENTITY";

/// Drag-and-drop payload type of assets: the asset's virtual path, zero-terminated (Asset Browser tiles).
inline constexpr const char* kAssetDragPayload = "ILLUSION_ASSET";

/// The asset path of an ILLUSION_ASSET payload (without its terminating zero).
[[nodiscard]] inline std::string_view assetPayloadPath(const ImGuiPayload& payload) noexcept
{
    const char* data = static_cast<const char*>(payload.Data);
    usize size = payload.DataSize > 0 ? static_cast<usize>(payload.DataSize) : 0;
    if (size > 0 && data[size - 1] == '\0')
    {
        --size;
    }
    return std::string_view(data, size);
}

} // namespace illusion::editor::shell::detail
