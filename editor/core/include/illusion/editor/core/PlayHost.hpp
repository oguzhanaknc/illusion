#pragma once

/// @file
/// What the play, module, `input.inject` and `input.read` commands need from the editor's game
/// runtime: play sessions (ADR 0042) and game modules. The play service of the editor process
/// (`illusion::editor_play`, PlaySession) implements it and registers with EditorContext::setPlay(); without
/// one, the commands fail with ErrorKind::Unsupported.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/Operation.hpp>
#include <illusion/platform/Input.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    std::string type;      ///< "Button", "Axis" or "Vector2".
    bool down = false;     ///< Held.
    bool pressed = false;  ///< Went down in the last update.
    bool released = false; ///< Went up in the last update.
    Vec2 value{0.0f};      ///< Axis: x; Vector2: x and y; Button: 1 or 0 in x.
};

/// A game module loaded in the editor (module.list).
struct PlayModuleInfo
{
    std::string name;    ///< ModuleApi::name.
    std::string version; ///< Semantic version.
    std::string path;    ///< The library it was loaded from ("" for static modules).
    bool shared = true;  ///< Loaded from a shared library (hot-reloadable).
    u32 reloads = 0;     ///< Successful hot reloads.
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
};

} // namespace illusion::editor
