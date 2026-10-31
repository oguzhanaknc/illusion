#pragma once

/// @file
/// PlaySession: the editor's game runtime (ADR 0042). It implements the PlayHost of the `play.*`,
/// `input.inject`, `input.read` and `module.*` commands: it owns the editor's ModuleHost, runs one
/// GameSession at a time on an in-memory copy of the edit World, and applies the side effects of commands
/// that change the play World (edit hooks).
///
/// @code
/// illusion::editor::PlaySession play(editor, {.jobs = &jobs, .audioOutput = illusion::AudioOutput::Null});
/// while (running)
/// {
///     play.update(frameSeconds); // advances the play World while a session runs
///     editor.endFrame(frameSeconds);
/// }
/// @endcode

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/modules/ModuleHost.hpp>

#include <memory>

namespace illusion
{
class AssetStreamer;
class GameSession;
class GamepadSystem;
class JobSystem;
class RenderAssetCache;
} // namespace illusion

namespace illusion::editor
{

class EditorContext;
class SettingsCatalog;

/// Creation parameters of a PlaySession. Everything it points at must outlive the session.
struct PlaySessionDesc
{
    JobSystem* jobs = nullptr;                     ///< Runs the game's jobs and the modules. Required.
    AssetStreamer* streamer = nullptr;             ///< Background loading for play sessions (null: none).
    RenderAssetCache* renderAssets = nullptr;      ///< GPU assets of the play World (null: headless).
    GamepadSystem* gamepads = nullptr;             ///< Gamepads the game reads (null: none).
    AudioOutput audioOutput = AudioOutput::Device; ///< Where the sound of play sessions goes.
    ModuleHostDesc modules{};                      ///< Parameters of the editor's ModuleHost.
};

/// Registers the project settings of the runtime (`illusion.PhysicsSettings` in `settings:/physics.json`,
/// and the audio, input and render settings) in @p catalog, reflecting them first. Types it has already are
/// kept.
void registerRuntimeSettings(SettingsCatalog& catalog);

/// The editor's game runtime. Registers itself with EditorContext::setPlay() and attaches the edit World to
/// its ModuleHost as WorldRole::Edit; the destructor stops a running session and undoes both. Use it on the
/// editor thread.
class PlaySession final : public PlayHost, NonCopyable
{
public:
    /// Creates the runtime of @p editor, which must outlive it, and registers the runtime settings
    /// (registerRuntimeSettings()).
    PlaySession(EditorContext& editor, const PlaySessionDesc& desc);

    /// Stops a running session, detaches the edit World and unregisters from the editor.
    ~PlaySession() override;

    /// Advances a playing session by @p deltaSeconds times the time scale (one fixed step with fixedStep).
    /// Call it once per editor frame, before EditorContext::endFrame(). Does nothing when stopped or paused.
    void update(f64 deltaSeconds);

    /// The running game, or nullptr when stopped.
    [[nodiscard]] GameSession* game() noexcept;

    /// The editor's modules.
    [[nodiscard]] ModuleHost& moduleHost() noexcept;

    // PlayHost
    [[nodiscard]] Expected<void, CommandError> start(const PlayStartOptions& options) override;
    void stop() override;
    void setPaused(bool paused) override;
    u32 step(u32 steps) override;
    void setTimeScale(f64 scale) override;
    [[nodiscard]] PlayStatus status() const override;
    [[nodiscard]] Expected<void, CommandError> inject(const PlayInputEvent& event) override;
    void actionStates(std::vector<PlayActionState>& out) const override;
    void playWorldEdited(std::span<const Operation> operations) override;
    [[nodiscard]] Expected<std::string, CommandError> loadModule(std::string_view path) override;
    [[nodiscard]] Expected<void, CommandError> reloadModule(std::string_view name) override;
    u32 reloadChangedModules() override;
    bool unloadModule(std::string_view name) override;
    void modules(std::vector<PlayModuleInfo>& out) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor
