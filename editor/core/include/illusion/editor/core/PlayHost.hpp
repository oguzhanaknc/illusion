#pragma once

/// @file
/// What the play, module, `saves`, `ai`, `input.inject` and `input.read` commands need from the editor's game
/// runtime: play sessions (ADR 0042) and game modules. The play service of the editor process
/// (`illusion::editor_play`, PlaySession) implements it and registers with EditorContext::setPlay(); without
/// one, the commands fail with ErrorKind::Unsupported.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/Operation.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/save/SaveService.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class AiWorld;
class GameSession;
class UiWorld;
struct UiEvent;
struct UiFrame;
namespace text
{
class Localization;
}
} // namespace illusion

namespace illusion::editor
{

/// Whether a play session runs.
enum class PlayState : u8
{
    Stopped, ///< Editing: there is no play World.
    Playing, ///< The play World advances every editor frame.
    Paused,  ///< The play World exists but advances only through steps.
};

/// What a play session runs.
enum class PlayMode : u8
{
    Play,     ///< The whole game: input, modules, physics, audio.
    Simulate, ///< Physics only: no input and no game code.
};

/// Stable lower-case name of @p state: "stopped", "playing" or "paused".
[[nodiscard]] std::string_view playStateName(PlayState state) noexcept;

/// Stable lower-case name of @p mode: "play" or "simulate".
[[nodiscard]] std::string_view playModeName(PlayMode mode) noexcept;

/// How a play session starts.
struct PlayStartOptions
{
    PlayMode mode = PlayMode::Play; ///< What runs.
    /// Deterministic: every frame and step advances exactly one physics step, and runtime spawns get ids
    /// from @ref seed.
    bool fixedStep = false;
    u64 seed = 0; ///< Seed of the ids of runtime spawns (fixedStep).
};

/// The state of the play session.
struct PlayStatus
{
    PlayState state = PlayState::Stopped; ///< Whether it runs.
    PlayMode mode = PlayMode::Play;       ///< What it runs (when not stopped).
    bool fixedStep = false;               ///< Deterministic steps (when not stopped).
    f64 timeScale = 1.0;                  ///< Multiplier of the editor's frame time.
    u64 steps = 0;                        ///< Updates of the play World so far.
    f64 time = 0.0;                       ///< Game time so far, in seconds.
};

/// A change of keyboard or mouse input that a play session's game sees (input.inject).
struct PlayInputEvent
{
    /// What changes.
    enum class Kind : u8
    {
        Key,           ///< @ref key goes down or up.
        MouseButton,   ///< @ref button goes down or up.
        MousePosition, ///< The cursor moves to @ref position (pixels in the game view).
    };
    Kind kind = Kind::Key;                  ///< What changes.
    Key key = Key::Unknown;                 ///< Kind::Key: the key.
    MouseButton button = MouseButton::Left; ///< Kind::MouseButton: the button.
    bool down = true;                       ///< Key and MouseButton: pressed (true) or released.
    Vec2 position{0.0f};                    ///< Kind::MousePosition: the cursor.
};

/// The state of one input action of the running game (input.read).
struct PlayActionState
{
    std::string name;      ///< The action.
    std::string type;      ///< "Button", "Axis", "Vector2" or "Delta".
    bool down = false;     ///< Held.
    bool pressed = false;  ///< Went down in the last update.
    bool released = false; ///< Went up in the last update.
    Vec2 value{0.0f};      ///< Axis: x; Vector2: x and y; Button: 1 or 0 in x.
};

/// A Component type whose layout a module load or hot reload changed, and what happened to its Components
/// (docs/adr/0044-component-layout-migration-on-hot-reload.md).
struct PlayComponentMigration
{
    std::string type{};                       ///< Stable name of the type.
    u32 components = 0;                       ///< Components migrated, in all Worlds.
    std::vector<std::string> addedFields{};   ///< New fields, which got their default value.
    std::vector<std::string> removedFields{}; ///< Fields of the old version that were dropped.
    std::vector<std::string> resetFields{};   ///< Fields reset to their default where the value did not fit.
};

/// Number of audio buses of a play session's mixer: Sfx, Music, Ambience, Dialogue and Ui (AudioBus order).
inline constexpr u32 kPlayAudioBuses = 5;

/// The physics and audio of a running play session (`play.diagnostics`).
struct PlayDiagnostics
{
    bool running = false;    ///< A play session runs; the other fields are zero otherwise.
    u32 bodies = 0;          ///< Physics bodies (characters count one each).
    u32 characters = 0;      ///< Character controllers.
    u32 joints = 0;          ///< Joints.
    u64 physicsSteps = 0;    ///< Fixed physics steps so far.
    u32 contacts = 0;        ///< Contact events of the last step.
    u32 activeVoices = 0;    ///< Sounds playing (or paused on a bus).
    u32 droppedSounds = 0;   ///< Sounds that could not start: every voice had a higher priority.
    u32 stolenVoices = 0;    ///< Voices taken from lower-priority sounds.
    u32 clips = 0;           ///< Loaded audio clips.
    u32 loadingClips = 0;    ///< Clips loading in the background.
    u64 clipBytes = 0;       ///< Memory held by loaded clips.
    f32 masterVolume = 1.0f; ///< Linear gain of the whole mix.
    std::array<f32, kPlayAudioBuses> busVolumes{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear gain per bus.
    std::array<bool, kPlayAudioBuses> busPaused{};                             ///< Paused buses.
};

/// A game module loaded in the editor (module.list).
struct PlayModuleInfo
{
    std::string name;    ///< ModuleApi::name.
    std::string version; ///< Semantic version.
    std::string path;    ///< The library it was loaded from ("" for static modules).
    bool shared = true;  ///< Loaded from a shared library (hot-reloadable).
    u32 reloads = 0;     ///< Successful hot reloads.
    std::vector<PlayComponentMigration> migrated{}; ///< Types the last load or reload migrated.
};

/// The result of one saved-game request of the running game, as the editor saw it (for the saves commands).
struct PlaySaveEvent
{
    SaveEventType type = SaveEventType::Failed; ///< What happened.
    SaveError error = SaveError::IoError;       ///< Why, when type is Failed.
    std::string slot{};                         ///< The slot of the request.
};

/// The editor's game runtime: one play session at a time and the loaded game modules. Called on the
/// editor thread.
class PlayHost
{
public:
    virtual ~PlayHost() = default;

    /// Takes an in-memory scene snapshot of the edit World, ids kept, loads it into a fresh play World with
    /// the project settings and attaches it to the EditorContext (EditorContext::attachPlayWorld()).
    /// PreconditionFailed while a session runs; ValidationFailed when the settings or the input actions
    /// asset cannot be read.
    [[nodiscard]] virtual Expected<void, CommandError> start(const PlayStartOptions& options) = 0;

    /// Detaches and destroys the play World; the edit World is as it was before start(). Does nothing when
    /// stopped.
    virtual void stop() = 0;

    /// Pauses (true) or resumes the running session. Ignored when stopped.
    virtual void setPaused(bool paused) = 0;

    /// Advances the running session by @p steps fixed physics steps at once, paused or not. Returns the
    /// steps taken (0 when stopped).
    virtual u32 step(u32 steps) = 0;

    /// Multiplies the frame time the session advances by (not its fixed steps); kept across sessions.
    virtual void setTimeScale(f64 scale) = 0;

    /// The state of the session.
    [[nodiscard]] virtual PlayStatus status() const = 0;

    /// Applies @p event to the keyboard and mouse of the running game. PreconditionFailed when stopped.
    [[nodiscard]] virtual Expected<void, CommandError> inject(const PlayInputEvent& event) = 0;

    /// Appends the actions of the running game to @p out, in id order.
    virtual void actionStates(std::vector<PlayActionState>& out) const = 0;

    /// Applies the side effects of @p operations, which a command just applied to the play World (edit
    /// hooks: rebuilt physics bodies, restarted sounds).
    virtual void playWorldEdited(std::span<const Operation> operations) = 0;

    /// Loads the game module at @p path (a shared library) and starts it on a running session. Returns its
    /// name; ModuleError with the host's message when it cannot be loaded.
    [[nodiscard]] virtual Expected<std::string, CommandError> loadModule(std::string_view path) = 0;

    /// Hot-reloads the module @p name from its library. NotFound when no module has that name; ModuleError
    /// with the host's message when the new library is rejected (the old code keeps running).
    [[nodiscard]] virtual Expected<void, CommandError> reloadModule(std::string_view name) = 0;

    /// Hot-reloads every shared module whose library file changed; returns how many were reloaded.
    virtual u32 reloadChangedModules() = 0;

    /// Unloads the module @p name; false if no module has that name.
    virtual bool unloadModule(std::string_view name) = 0;

    /// Appends the loaded modules to @p out, in load order.
    virtual void modules(std::vector<PlayModuleInfo>& out) const = 0;

    /// The physics and audio of the running session (PlayDiagnostics::running is false when stopped).
    [[nodiscard]] virtual PlayDiagnostics diagnostics() const = 0;

    /// Sets the master volume (linear gain, at least 0) of the running session's mixer, for this session only
    /// (the project's audio settings keep theirs). Does nothing when stopped.
    virtual void setMasterVolume(f32 volume) = 0;

    /// Sets the volume (linear gain, at least 0) of @p bus (< kPlayAudioBuses) and whether it is paused, for
    /// this session only. Does nothing when stopped.
    virtual void setBus(u32 bus, f32 volume, bool paused) = 0;

    /// The running game, or nullptr when stopped (the Game view draws its UI).
    [[nodiscard]] virtual GameSession* game() noexcept = 0;

    /// The UI of the running game as its last update laid it out (the gameui.* commands), or nullptr when
    /// stopped.
    [[nodiscard]] virtual UiWorld* gameUi() noexcept = 0;

    /// The target the running game lays its UI out for: the Game view's image, 1920x1080 until a view sets
    /// one (a headless editor). Empty when stopped.
    [[nodiscard]] virtual UiFrame gameUiFrame() const = 0;

    /// Clicks the UI of the running game at @p pixels of gameUiFrame(), paused or not: one update in which
    /// the UI's pointer is there with the primary button held, one in which it is released. The game's
    /// keyboard and mouse are not changed (its input actions see no click), and from the next update on the
    /// UI reads the real cursor again. Appends the UI's events of both updates to @p events and returns the
    /// updates run: fewer when the game ended, 0 when stopped.
    virtual u32 clickUi(Vec2 pixels, std::vector<UiEvent>& events) = 0;

    /// The game's string tables of the open project (loadGameLocalization()), for gameui.layout on the edit
    /// World: read on first use and again after a locale file changed or a scene was opened. Valid until
    /// the next call.
    [[nodiscard]] virtual const text::Localization* projectLocalization() = 0;

    /// The saved games of the running game (GameSession::saves()), or nullptr when stopped or when the game
    /// has no SaveSystem. Requests made through it are sent at the next update.
    [[nodiscard]] virtual SaveService* saves() noexcept = 0;

    /// The AI of the running game (GameSession::ai()), or nullptr when stopped (the AI commands and the
    /// perception view).
    [[nodiscard]] virtual AiWorld* ai() noexcept = 0;

    /// Number of saved-game events the editor has seen since it started, over all sessions; the next one gets
    /// this index. Events are those of every update the session ran (also steps), so none is missed.
    [[nodiscard]] virtual u64 saveEventCount() const = 0;

    /// Appends the events with an index of @p first or more that are still kept (the last 256) to @p out, in
    /// order.
    virtual void saveEvents(u64 first, std::vector<PlaySaveEvent>& out) const = 0;
};

} // namespace illusion::editor
