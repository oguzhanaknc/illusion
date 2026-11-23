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
///     (*session)->setUiFrame({viewSize});
///     (*session)->update(frameSeconds);
///     // render the scene, then uiRenderer->render(commands, view, (*session)->ui().drawData())
/// }
/// @endcode

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <memory>
#include <optional>
#include <string>

namespace illusion
{

class AiWorld;
class AnimationWorld;
class AssetStreamer;
class CameraWorld;
class GamepadSystem;
class InputActions;
class InputState;
class JobSystem;
class ModuleHost;
class NavWorld;
class ParticleWorld;
class PhysicsWorld;
class RenderAssetCache;
class SaveService;
class SaveSystem;
class StoreService;
class UiAssets;
class UiWorld;
class VirtualFileSystem;
class World;
struct UiFrame;

namespace text
{
class Localization;
}

/// A pointer that the UI of a GameSession sees instead of the cursor (GameSession::setUiPointer()).
struct GameUiPointer
{
    Vec2 pixels{0.0f}; ///< Position in the pixels of the UI frame.
    bool down = false; ///< The primary button is held.
};

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
    /// Loads audio clips, physics meshes and animated models in the background; the host calls its update()
    /// once per frame. Null loads clips on the calling thread, leaves mesh colliders without a shape and
    /// animates nothing (animation() is null).
    AssetStreamer* streamer = nullptr;
    /// GPU meshes and materials for rendering the World; null (headless) resolves no render Components.
    RenderAssetCache* renderAssets = nullptr;
    /// Gamepads that drive the input actions; the host calls its update(). Null for none.
    GamepadSystem* gamepads = nullptr;
    /// The store the modules see as StoreService (ADR 0056); the host calls its update(). Null for none.
    StoreService* store = nullptr;
    /// The saved games the modules see as SaveService (ADR 0058); null provides no SaveService. One session
    /// at a time may use it.
    SaveSystem* saves = nullptr;
    /// True when store keeps its cloud files remotely (StoreBackend::cloudIsRemote()); slot files are then
    /// mirrored to it unless SaveSettings::cloud is off.
    bool storeCloudIsRemote = false;
    /// Fonts and images of the UI (UiRenderAssets); null (headless) lays the UI out with solid colours only.
    UiAssets* uiAssets = nullptr;
    AudioOutput audioOutput = AudioOutput::Device; ///< Where the session's sound goes.
    /// Deterministic fixed steps: every update() advances exactly one physics step whatever the time
    /// passed, and Entities created while running get EntityGuids from a generator seeded with seed. The
    /// same scene, input and seed then give the same World.
    bool fixedStep = false;
    u64 seed = 0; ///< Seed of the EntityGuids of Entities created while running (fixedStep).
};

/// One running game: a World with its PhysicsWorld, AudioWorld, InputActions, UiWorld, asset resolvers and
/// game modules. Each update() runs, in this order:
/// 1. **Input:** the InputActions read keyboardMouse() and the gamepads (Play mode, with the focus). While
///    the last update showed a modal canvas (UiWorld::modalActive()) or left a UiTextField with the focus,
///    only the `ui.*` actions get input.
/// 2. **Modules:** ModuleHost::update() on the World (Play mode); then Entities created without an
///    EntityGuid get one.
/// 3. **Saves:** (Play mode, with GameSessionDesc::saves, while started) the loads that the save thread
///    finished are applied to the World (applySave()) and the finished requests become the events of
///    SaveService::pollEvents() for the next update; then the save(), load() and remove() calls of the
///    Modules step are sent in call order, each save() capturing the World now (captureSave()). A load is
///    therefore applied in a later update than its request; with fixedStep the step first waits for the save
///    thread, so it is always the next one and fixed-step runs repeat.
/// 4. **AI:** AiWorld::update() (ADR 0062; with fixedStep it first waits for the navmeshes and the
///    behaviour tree loads, NavWorld::waitIdle() and AiWorld::waitIdle()) perceives with line-of-sight
///    raycasts through physics() and ticks the behaviour trees while started; their moves become the
///    NavAgents' destinations for the Navigation step.
/// 5. **Navigation:** NavWorld::update() (ADR 0062) moves the crowd while started. Then, while started,
///    a NavAgent whose drive is Character gives its CharacterController the horizontal part of
///    NavAgentState::velocity (PhysicsWorld::setCharacterVelocity(), keeping its vertical velocity; none
///    while it crosses a NavLink, when navigation moves its Transform), and a NavAgent with a speedParameter
///    sets that Float parameter of its animation graph to NavAgentState::speed.
/// 6. **Animation:** (with GameSessionDesc::streamer) while started, foot placement (ADR 0061) first casts a
///    ray down below every foot of a FootPlacement from the last update's pose (through
///    PhysicsWorld::raycast(), skipping the character's own bodies) and writes the lifts, tilts, weights and
///    pelvis offset of its FootPlacementPose. AnimationWorld::update() then poses the Animators; their time
///    and graphs advance while the session is started. Then, while started, root motion (ADR 0060): an Entity
///    whose pose has AnimationPose::rootTranslation or AnimationPose::rootYaw turns about +y by the yaw; with
///    a CharacterController it gets the translation divided by the step as its horizontal velocity
///    (PhysicsWorld::setCharacterVelocity(), keeping its vertical velocity), otherwise the translation is
///    added to its Transform (and its LocalTransform follows when it has a parent); the translation of a
///    NavAgent whose drive is Character or Transform is not applied. The Entities attached to
///    joints (BoneAttachment, ADR 0061) then take their Transforms from their moved parents, so that
///    kinematic bodies on them follow the sockets in the physics step. An active Ragdoll suspends root
///    motion.
/// 7. **Physics:** (with GameSessionDesc::streamer) a Ragdoll (ADR 0061) that turned active gets its ragdoll
///    (PhysicsWorld::startRagdoll()) from the pose and motion of the Animation step, with the profile of
///    Ragdoll::profile (loaded by the physics asset resolver) or one made from the skeleton
///    (makeRagdollProfile()); one that turned off lets the AnimationWorld blend back to the animation. Then
///    PhysicsWorld::update(), and the ragdoll poses go to AnimationWorld::setRagdollPose() for the next
///    Animation step.
/// 8. **Hierarchy:** updateTransformHierarchy().
/// 9. **Cameras:** while started, CameraWorld::update() (ADR 0064) moves every CameraBrain's Camera to its
///    live VirtualCamera, blending and shaking; spring arms cast spheres through physics() that pass through
///    the follow target's own bodies (PhysicsCameraCollision), orbits and arms read their look actions in
///    Play mode, and the composer frames for the aspect of setUiFrame() (16:9 while it is empty).
/// 10. **UI:** the UiWorld lays out for the frame of setUiFrame(), handles the pointer and wheel of
///    keyboardMouse() and the actions `ui.navigate`, `ui.submit` and `ui.cancel` (Play mode), and records
///    the draw list (ui()). UiGlyph labels come from the actions: the first binding for the device used
///    last, through localization().
/// 11. **Particles and audio:** ParticleWorld::update() and AudioWorld::update().
/// 12. **Resolvers:** the physics and render asset resolvers turn asset references into runtime Components.
///
/// Modules find the services PhysicsService, AudioService, ParticleService, InputService, SceneService,
/// UiService (over ui() and localization()), NavService (over navigation()), AiService (over ai()), with
/// GameSessionDesc::streamer AnimationService, with GameSessionDesc::store StoreService and with
/// GameSessionDesc::saves SaveService.
///
/// The session's Localization (localization()) holds the string tables of the project's locale files,
/// `assets:/locales/*.json`, read by create(): the keys of every file, with the English text of `en.json`
/// (or the first file that has the key); UiText::key is looked up there. It also holds the input table of key
/// and gamepad labels (illusion::input::strings::kTable), which UiGlyph shows.
///
/// While the last update left a UiTextField with the focus, the keyboard edits it: the UI gets the typed
/// characters of keyboardMouse(), Left, Right, Backspace and Delete (repeating after 0.4 s every 0.05 s
/// while held), Home and End. The keyboard bindings of `ui.navigate` and `ui.submit` are then ignored, so
/// that letters bound to them (W, A, S, D, Space) are typed: Up and Down move the focus, Enter submits,
/// and only the gamepad bindings of both actions still count (gamepad left and right move the caret).
/// `ui.cancel` keeps all its bindings.
///
/// Not thread-safe: create, update and destroy it on one thread.
class GameSession : NonCopyable
{
public:
    /// Creates a session with an empty World: its systems take their parameters from desc.settings, and the
    /// actions come from desc.settings.input.actions, plus `ui.navigate`, `ui.submit` and `ui.cancel` with
    /// their default bindings when the asset lacks them. Fails, saying why, if that asset cannot be loaded.
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
    /// started; update() still runs the other steps afterwards. Save requests not sent yet are dropped; the
    /// writes and removals already sent finish on the save thread (with a cloud mirror, stop() waits for them
    /// and mirrors them), but no event of them is reported and no load is applied after stop().
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

    /// The particle effects of the World, advanced after physics with its gravity.
    [[nodiscard]] ParticleWorld& particles() noexcept;

    /// The poses of the World's Animators, for the renderer and host tools; null without
    /// GameSessionDesc::streamer.
    [[nodiscard]] AnimationWorld* animation() noexcept;

    /// The navmeshes and the crowd of the World; without GameSessionDesc::streamer navmeshes are baked, never
    /// loaded.
    [[nodiscard]] NavWorld& navigation() noexcept;

    /// The perception and behaviour trees of the World; without GameSessionDesc::streamer trees never load.
    [[nodiscard]] AiWorld& ai() noexcept;

    /// The virtual cameras of the World and their brains.
    [[nodiscard]] CameraWorld& cameras() noexcept;

    /// Sets the target the UI is laid out for from the next update() on (the host's view size and safe
    /// insets). Starts empty. The cursor of keyboardMouse() times @p pointerScale is the pointer in the
    /// pixels of @p frame: the player passes the frame's size over the window's, the editor's Game view
    /// writes the cursor in the pixels of its image and passes 1.
    void setUiFrame(const UiFrame& frame, Vec2 pointerScale = Vec2(1.0f)) noexcept;

    /// The target of setUiFrame().
    [[nodiscard]] UiFrame uiFrame() const noexcept;

    /// Replaces the cursor and primary button of keyboardMouse() as the UI's pointer from the next update()
    /// on, until std::nullopt (the default) gives them back; keyboardMouse() itself is not changed, so the
    /// game's input actions do not see it. For tools that act on the UI as a click would (the editor's
    /// `gameui.click`).
    void setUiPointer(const std::optional<GameUiPointer>& pointer) noexcept;

    /// The UI of the World; its drawData() is what the last update() recorded.
    [[nodiscard]] UiWorld& ui() noexcept;

    /// The SaveService the modules see, for host tools (the saves commands of the editor); requests made
    /// through it between updates are sent at the Saves step of the next update. Null without
    /// GameSessionDesc::saves.
    [[nodiscard]] SaveService* saves() noexcept;

    /// The game's string tables and active language.
    [[nodiscard]] text::Localization& localization() noexcept;

    /// What the session runs.
    [[nodiscard]] GameSessionMode mode() const noexcept;

    /// The settings it was created with.
    [[nodiscard]] const ProjectSettings& settings() const noexcept;

    /// True once a module asked to end the game (SceneService::requestExit()); the host then ends it after
    /// the update: the player closes, the editor's Play mode stops.
    [[nodiscard]] bool exitRequested() const noexcept;

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
