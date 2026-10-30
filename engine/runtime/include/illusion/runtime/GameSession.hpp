#pragma once

/// @file
/// GameSession: one running game. It owns the World the game plays in and every system that runs it, and
/// advances them in a fixed order. The editor's Play mode and the exported player run the same GameSession
/// (ADR 0042).
///
/// @code
/// illusion::GameSessionDesc desc;
/// desc.jobs = &jobs;
/// desc.files = &files;
/// desc.modules = &modules;
/// desc.settings = settings; // loadProjectSettings()
/// auto session = illusion::GameSession::create(desc);
/// if (!session)
/// {
///     return fail(session.error());
/// }
/// illusion::loadScene((*session)->world(), sceneJson);
/// (*session)->start();
/// while (running)
/// {
///     copyWindowInput((*session)->keyboardMouse());
///     (*session)->update(frameSeconds);
/// }
/// @endcode

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <memory>
#include <string>

namespace illusion
{

class AssetStreamer;
class GamepadSystem;
class InputActions;
class InputState;
class JobSystem;
class ModuleHost;
class PhysicsWorld;
class RenderAssetCache;
class VirtualFileSystem;
class World;

/// What a GameSession runs.
enum class GameSessionMode : u8
{
    Play,     ///< The whole game: input, modules, physics, hierarchy, audio and asset resolution.
    Simulate, ///< Everything but input and modules: physics moves bodies, no game code runs.
};

/// Creation parameters of a GameSession. Everything it points at must outlive the session.
struct GameSessionDesc
{
    JobSystem* jobs = nullptr;                    ///< Runs physics, hierarchy and asset jobs. Required.
    const VirtualFileSystem* files = nullptr;     ///< Reads input actions, scenes and audio clips. Required.
    ProjectSettings settings{};                   ///< The project settings (loadProjectSettings()).
    GameSessionMode mode = GameSessionMode::Play; ///< What runs.
    /// Game modules, attached to the session's World with WorldRole::Simulation in Play mode (start() and
    /// stop() run their start and stop). Null runs no game code.
    ModuleHost* modules = nullptr;
    /// Loads audio clips and physics meshes in the background; the host calls its update() once per frame.
    /// Null loads clips on the calling thread and leaves mesh colliders without a shape.
    AssetStreamer* streamer = nullptr;
    /// GPU meshes and materials for rendering the World; null (headless) resolves no render Components.
    RenderAssetCache* renderAssets = nullptr;
    /// Gamepads that drive the input actions; the host calls its update(). Null for none.
    GamepadSystem* gamepads = nullptr;
    AudioOutput audioOutput = AudioOutput::Device; ///< Where the session's sound goes.
    /// Deterministic fixed steps: every update() advances exactly one physics step whatever the time
    /// passed, and Entities created while running get EntityGuids from a generator seeded with seed. The
    /// same scene, input and seed then give the same World.
    bool fixedStep = false;
    u64 seed = 0; ///< Seed of the EntityGuids of Entities created while running (fixedStep).
};

/// One running game: a World with its PhysicsWorld, AudioWorld, InputActions, asset resolvers and game
/// modules. Each update() runs, in this order:
/// 1. **Input:** the InputActions read keyboardMouse() and the gamepads (Play mode, with the focus).
/// 2. **Modules:** ModuleHost::update() on the World (Play mode); then Entities created without an
///    EntityGuid get one.
/// 3. **Physics:** PhysicsWorld::update().
/// 4. **Hierarchy:** updateTransformHierarchy().
/// 5. **Audio:** AudioWorld::update().
/// 6. **Resolvers:** the physics and render asset resolvers turn asset references into runtime Components.
///
/// Not thread-safe: create, update and destroy it on one thread.
class GameSession : NonCopyable
{
public:
    /// Creates a session with an empty World: its systems take their parameters from desc.settings, and the
    /// actions come from desc.settings.input.actions. Fails, saying why, if that asset cannot be loaded.
    [[nodiscard]] static Expected<std::unique_ptr<GameSession>, std::string>
    create(const GameSessionDesc& desc);

    /// Stops the session (stop()) and destroys its systems, then the World.
    ~GameSession();

    /// The World the game plays in. Load the scene into it before start().
    [[nodiscard]] World& world() noexcept;

    /// The World the game plays in.
    [[nodiscard]] const World& world() const noexcept;

    /// Attaches the World to the ModuleHost (Play mode with modules), which runs each module's start().
    /// Does nothing when already started.
    void start();

    /// Detaches the World from the ModuleHost, which runs each module's stop(). Does nothing when not
    /// started; update() still runs the other steps afterwards.
    void stop();

    /// True between start() and stop().
    [[nodiscard]] bool isStarted() const noexcept;

    /// Advances the game by @p deltaSeconds (by one physics step with GameSessionDesc::fixedStep), in the
    /// order above, then clears the input edges of keyboardMouse().
    void update(f64 deltaSeconds);

    /// The keyboard and mouse the game sees. The host writes the window's input (or synthetic input, such
    /// as the editor's Game view or a test) into it between updates; update() clears its edges afterwards.
    [[nodiscard]] InputState& keyboardMouse() noexcept;

    /// Whether the game has the input focus. Without it, the actions see no keyboard and mouse, and no
    /// gamepads unless InputSettings::gamepadsInBackground. Starts with the focus.
    void setInputFocus(bool focus) noexcept;

    /// True if the game has the input focus.
    [[nodiscard]] bool hasInputFocus() const noexcept;

    /// The game's input actions.
    [[nodiscard]] InputActions& actions() noexcept;

    /// The physics simulation of the World.
    [[nodiscard]] PhysicsWorld& physics() noexcept;

    /// The audio of the World.
    [[nodiscard]] AudioWorld& audio() noexcept;

    /// What the session runs.
    [[nodiscard]] GameSessionMode mode() const noexcept;

    /// The settings it was created with.
    [[nodiscard]] const ProjectSettings& settings() const noexcept;

    /// Number of update() calls so far.
    [[nodiscard]] u64 updateCount() const noexcept;

    /// Game time so far: the sum of the update() steps, in seconds.
    [[nodiscard]] f64 time() const noexcept;

private:
    struct Impl;
    explicit GameSession(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
