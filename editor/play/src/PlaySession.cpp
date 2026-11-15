#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/SettingsCatalog.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/runtime/GameLocalization.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/store/NullStoreService.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <algorithm>
#include <array>

namespace illusion::editor
{
namespace
{

struct RuntimeSettingsFile
{
    std::string_view type;
    std::string_view file;
};

constexpr std::array<RuntimeSettingsFile, 5> kRuntimeSettings{{
    {"illusion.PhysicsSettings", kPhysicsSettingsFile},
    {"illusion.AudioSettings", kAudioSettingsFile},
    {"illusion.InputSettings", kInputSettingsFile},
    {"illusion.RenderSettings", kRenderSettingsFile},
    {"illusion.WindowSettings", kWindowSettingsFile},
}};

// Components whose changes need the physics body rebuilt.
constexpr std::array<std::string_view, 4> kPhysicsTypes{"illusion.RigidBody", "illusion.Collider",
                                                        "illusion.CharacterController", "illusion.Joint"};

constexpr std::string_view kAudioSource = "illusion.AudioSource";

// Paths of the game's locale files (loadGameLocalization()).
constexpr std::string_view kLocalePrefix = "assets:/locales/";

// The UI frame of a session until the Game view sets its own (a headless editor has none).
constexpr UVec2 kDefaultUiFrame{1920, 1080};

// Play mode has no store (ADR 0056); the editor has no user mount, so the cloud files go to the project's
// cache.
constexpr std::string_view kPlayCloudDirectory = "cache:/store/cloud";

// The "clip" member of an AudioSource value ("" when absent).
std::string clipOf(const std::optional<std::string>& value)
{
    if (!value)
    {
        return {};
    }
    Expected<JsonDocument, JsonError> json = JsonDocument::parse(*value);
    if (!json)
    {
        return {};
    }
    return std::string(json->root().find("clip").asString().value_or(""));
}

} // namespace

void registerRuntimeSettings(SettingsCatalog& catalog)
{
    reflectRuntimeStructs();
    for (const RuntimeSettingsFile& settings : kRuntimeSettings)
    {
        if (catalog.find(settings.type, SettingsScope::Project) == nullptr)
        {
            catalog.add(settings.type, SettingsScope::Project, settings.file);
        }
    }
}

struct PlaySession::Impl
{
    Impl(EditorContext& editorContext, const PlaySessionDesc& sessionDesc)
        : editor(editorContext)
        , desc(sessionDesc)
        , modules(*sessionDesc.jobs, sessionDesc.modules)
    {
    }

    EditorContext& editor;
    PlaySessionDesc desc;
    ModuleHost modules;
    std::unique_ptr<NullStoreService> store; // one per session, so achievements and stats start empty
    std::unique_ptr<GameSession> game;
    PlayStartOptions options;
    bool paused = false;
    f64 timeScale = 1.0;
    // The project's string tables for the edit World's UI (projectLocalization()), read again when stale.
    std::unique_ptr<GameStringTable> projectStrings;
    std::unique_ptr<text::Localization> projectTexts;
    bool projectTextsStale = true;
    u32 assetSubscription = 0;
    u32 sceneSubscription = 0;

    // One update of a fixed physics step, with the background loads finished first (step() and clickUi()).
    void stepOnce() const
    {
        pumpAssets(true);
        store->update();
        game->update(game->settings().physics.stepSeconds);
    }

    // Publishes the background loads (audio clips, collision meshes) before the game advances; @p wait
    // finishes them first, so that steps and fixed-step sessions are the same on every run.
    void pumpAssets(bool wait) const
    {
        if (desc.streamer == nullptr)
        {
            return;
        }
        if (wait)
        {
            desc.streamer->waitIdle();
        }
        else
        {
            (void)desc.streamer->update();
        }
    }
};

PlaySession::PlaySession(EditorContext& editor, const PlaySessionDesc& desc)
{
    ILL_ASSERT(desc.jobs != nullptr, "a PlaySession needs a JobSystem");
    m_impl = std::make_unique<Impl>(editor, desc);
    registerRuntimeSettings(editor.settings());
    m_impl->modules.attachWorld(editor.world(), WorldRole::Edit);
    // Another project's scene, or changed locale files, make the project's string tables stale.
    m_impl->assetSubscription =
        editor.events().subscribe("asset.changed", [impl = m_impl.get()](const EditorEvent& event) {
            if (event.payload.find(kLocalePrefix) != std::string::npos)
            {
                impl->projectTextsStale = true;
            }
        });
    m_impl->sceneSubscription = editor.events().subscribe(
        "scene.opened", [impl = m_impl.get()](const EditorEvent&) { impl->projectTextsStale = true; });
    editor.setPlay(this);
}

PlaySession::~PlaySession()
{
    stop();
    m_impl->editor.events().unsubscribe(m_impl->assetSubscription);
    m_impl->editor.events().unsubscribe(m_impl->sceneSubscription);
    m_impl->editor.setPlay(nullptr);
    m_impl->modules.detachWorld(m_impl->editor.world());
}

GameSession* PlaySession::game() noexcept
{
    return m_impl->game.get();
}

ModuleHost& PlaySession::moduleHost() noexcept
{
    return m_impl->modules;
}

Expected<void, CommandError> PlaySession::start(const PlayStartOptions& options)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (impl.game)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "a play session runs already"));
    }
    GameSessionDesc desc;
    desc.jobs = impl.desc.jobs;
    desc.files = &impl.editor.files();
    if (Expected<void, std::string> loaded = loadProjectSettings(impl.editor.files(), desc.settings); !loaded)
    {
        return unexpected(CommandError::make(ErrorKind::ValidationFailed, loaded.error()));
    }
    desc.mode = options.mode == PlayMode::Simulate ? GameSessionMode::Simulate : GameSessionMode::Play;
    desc.modules = &impl.modules;
    desc.streamer = impl.desc.streamer;
    desc.renderAssets = impl.desc.renderAssets;
    desc.gamepads = impl.desc.gamepads;
    auto store = std::make_unique<NullStoreService>(impl.editor.files(),
                                                    NullStoreDesc{.cloudDirectory = kPlayCloudDirectory});
    desc.store = store.get();
    desc.audioOutput = impl.desc.audioOutput;
    desc.fixedStep = options.fixedStep;
    desc.seed = options.seed;
    Expected<std::unique_ptr<GameSession>, std::string> game = GameSession::create(desc);
    if (!game)
    {
        return unexpected(CommandError::make(ErrorKind::ValidationFailed, game.error()));
    }
    // The snapshot keeps the ids, so commands and the selection find the same entities in both Worlds.
    Expected<std::string, CommandError> scene = impl.editor.sceneJson(PrefabSaveMode::Expanded);
    if (!scene)
    {
        return unexpected(std::move(scene).error());
    }
    const SceneLoadResult loaded = loadScene((*game)->world(), *scene);
    if (!loaded.ok())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot copy the scene: " + loaded.error));
    }
    impl.store = std::move(store);
    impl.game = std::move(game).value();
    impl.options = options;
    impl.paused = false;
    impl.game->setUiFrame({kDefaultUiFrame});
    impl.editor.attachPlayWorld(impl.game->world());
    impl.game->start();
    // The gameui.* commands find the UI laid out before the first update (play.start then gameui.click).
    impl.game->ui().update(impl.game->uiFrame());
    ILL_LOG_INFO("Play", "Play session started ({}, {} entities)", playModeName(options.mode),
                 loaded.entities.size());
    return {};
}

void PlaySession::stop()
{
    Impl& impl = *m_impl;
    if (!impl.game)
    {
        return;
    }
    impl.editor.detachPlayWorld();
    impl.game.reset();
    impl.store.reset();
    impl.paused = false;
    ILL_LOG_INFO("Play", "Play session stopped");
}

void PlaySession::setPaused(bool paused)
{
    if (m_impl->game)
    {
        m_impl->paused = paused;
    }
}

u32 PlaySession::step(u32 steps)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (!impl.game)
    {
        return 0;
    }
    u32 done = 0;
    while (done < steps && !impl.game->exitRequested())
    {
        impl.stepOnce();
        ++done;
    }
    impl.editor.playWorldAdvanced();
    stopIfExitRequested();
    return done;
}

UiWorld* PlaySession::gameUi() noexcept
{
    return m_impl->game ? &m_impl->game->ui() : nullptr;
}

const text::Localization* PlaySession::projectLocalization()
{
    Impl& impl = *m_impl;
    if (impl.projectTextsStale)
    {
        // The old Localization goes before the table it views.
        auto strings = std::make_unique<GameStringTable>();
        auto texts = std::make_unique<text::Localization>();
        loadGameLocalization(impl.editor.files(), *texts, *strings);
        impl.projectTexts = std::move(texts);
        impl.projectStrings = std::move(strings);
        impl.projectTextsStale = false;
    }
    return impl.projectTexts.get();
}

UiFrame PlaySession::gameUiFrame() const
{
    return m_impl->game ? m_impl->game->uiFrame() : UiFrame{};
}

u32 PlaySession::clickUi(Vec2 pixels, std::vector<UiEvent>& events)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (!impl.game)
    {
        return 0;
    }
    u32 done = 0;
    for (const bool down : {true, false})
    {
        if (impl.game->exitRequested())
        {
            break;
        }
        impl.game->setUiPointer(GameUiPointer{.pixels = pixels, .down = down});
        impl.stepOnce();
        const std::span<const UiEvent> reported = impl.game->ui().events();
        events.insert(events.end(), reported.begin(), reported.end());
        ++done;
    }
    impl.game->setUiPointer(std::nullopt);
    impl.editor.playWorldAdvanced();
    stopIfExitRequested();
    return done;
}

void PlaySession::stopIfExitRequested()
{
    if (m_impl->game && m_impl->game->exitRequested())
    {
        ILL_LOG_INFO("Play", "the game asked to end");
        stop();
    }
}

void PlaySession::setTimeScale(f64 scale)
{
    m_impl->timeScale = scale;
}

void PlaySession::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (!impl.game || impl.paused || impl.timeScale <= 0.0)
    {
        return;
    }
    impl.pumpAssets(impl.options.fixedStep);
    impl.store->update();
    impl.game->update(deltaSeconds * impl.timeScale);
    impl.editor.playWorldAdvanced();
    stopIfExitRequested();
}

PlayStatus PlaySession::status() const
{
    const Impl& impl = *m_impl;
    PlayStatus status;
    status.timeScale = impl.timeScale;
    if (impl.game)
    {
        status.state = impl.paused ? PlayState::Paused : PlayState::Playing;
        status.mode = impl.options.mode;
        status.fixedStep = impl.options.fixedStep;
        status.steps = impl.game->updateCount();
        status.time = impl.game->time();
    }
    return status;
}

PlayDiagnostics PlaySession::diagnostics() const
{
    const Impl& impl = *m_impl;
    PlayDiagnostics out;
    if (!impl.game)
    {
        return out;
    }
    out.running = true;
    const PhysicsWorld& physics = impl.game->physics();
    out.bodies = physics.bodyCount();
    out.characters = physics.characterCount();
    out.joints = physics.jointCount();
    out.physicsSteps = physics.stepCount();
    out.contacts = static_cast<u32>(physics.contactEvents().size());
    const AudioWorld& audio = impl.game->audio();
    const AudioStats stats = audio.stats();
    out.activeVoices = stats.activeVoices;
    out.droppedSounds = stats.droppedSounds;
    out.stolenVoices = stats.stolenVoices;
    out.clips = stats.clips;
    out.loadingClips = stats.loadingClips;
    out.clipBytes = stats.clipBytes;
    out.masterVolume = audio.masterVolume();
    for (u32 bus = 0; bus < kPlayAudioBuses; ++bus)
    {
        out.busVolumes[bus] = audio.busVolume(static_cast<AudioBus>(bus));
        out.busPaused[bus] = audio.isBusPaused(static_cast<AudioBus>(bus));
    }
    return out;
}

void PlaySession::setMasterVolume(f32 volume)
{
    if (m_impl->game)
    {
        m_impl->game->audio().setMasterVolume(volume);
    }
}

void PlaySession::setBus(u32 bus, f32 volume, bool paused)
{
    ILL_ASSERT(bus < kPlayAudioBuses, "audio bus {} out of range", bus);
    if (m_impl->game)
    {
        AudioWorld& audio = m_impl->game->audio();
        audio.setBusVolume(static_cast<AudioBus>(bus), volume);
        audio.setBusPaused(static_cast<AudioBus>(bus), paused);
    }
}

Expected<void, CommandError> PlaySession::inject(const PlayInputEvent& event)
{
    Impl& impl = *m_impl;
    if (!impl.game)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no play session runs"));
    }
    InputState& input = impl.game->keyboardMouse();
    switch (event.kind)
    {
    case PlayInputEvent::Kind::Key:
        input.setKey(event.key, event.down);
        break;
    case PlayInputEvent::Kind::MouseButton:
        input.setMouseButton(event.button, event.down);
        break;
    case PlayInputEvent::Kind::MousePosition:
        input.setMousePosition(event.position);
        break;
    }
    return {};
}

void PlaySession::actionStates(std::vector<PlayActionState>& out) const
{
    const Impl& impl = *m_impl;
    if (!impl.game)
    {
        return;
    }
    const InputActions& actions = impl.game->actions();
    for (ActionId action = 0; action < actions.actionCount(); ++action)
    {
        PlayActionState state;
        state.name = actions.actionName(action);
        const ActionType type = actions.actionType(action);
        state.type = std::string(actionTypeName(type));
        state.down = actions.isDown(action);
        state.pressed = actions.wasPressed(action);
        state.released = actions.wasReleased(action);
        state.value = type == ActionType::Vector2 || type == ActionType::Delta
                          ? actions.vector(action)
                          : Vec2(actions.value(action), 0.0f);
        out.push_back(std::move(state));
    }
}

void PlaySession::playWorldEdited(std::span<const Operation> operations)
{
    Impl& impl = *m_impl;
    EntityIndex* index = impl.editor.playEntities();
    if (!impl.game || index == nullptr)
    {
        return;
    }
    for (const Operation& operation : operations)
    {
        if (const auto* change = std::get_if<ComponentChange>(&operation))
        {
            const Entity entity = index->find(change->entity);
            if (entity.isNull())
            {
                continue;
            }
            if (std::find(kPhysicsTypes.begin(), kPhysicsTypes.end(), change->type) != kPhysicsTypes.end())
            {
                impl.game->physics().rebuild(entity);
            }
            else if (change->type == kAudioSource && change->after &&
                     clipOf(change->before) != clipOf(change->after))
            {
                impl.game->audio().play(entity);
            }
        }
        else if (const auto* lifetime = std::get_if<EntityLifetime>(&operation);
                 lifetime != nullptr && lifetime->spawn)
        {
            // Created and pasted entities get their bodies on the next update.
            for (const EntityRecord& record : lifetime->entities)
            {
                if (const Entity entity = index->find(record.guid); !entity.isNull())
                {
                    impl.game->physics().rebuild(entity);
                }
            }
        }
    }
}

Expected<std::string, CommandError> PlaySession::loadModule(std::string_view path)
{
    Impl& impl = *m_impl;
    const std::filesystem::path library(std::u8string(path.begin(), path.end()));
    if (std::optional<std::string> error = impl.modules.load(library))
    {
        return unexpected(CommandError::make(ErrorKind::ModuleError, std::move(*error)));
    }
    return impl.modules.module(impl.modules.count() - 1).name;
}

Expected<void, CommandError> PlaySession::reloadModule(std::string_view name)
{
    Impl& impl = *m_impl;
    if (impl.modules.find(name) == nullptr)
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "no module '" + std::string(name) + "' is loaded"));
    }
    if (std::optional<std::string> error = impl.modules.reload(name))
    {
        return unexpected(CommandError::make(ErrorKind::ModuleError, std::move(*error)));
    }
    return {};
}

u32 PlaySession::reloadChangedModules()
{
    return m_impl->modules.reloadChanged();
}

bool PlaySession::unloadModule(std::string_view name)
{
    return m_impl->modules.unload(name);
}

void PlaySession::modules(std::vector<PlayModuleInfo>& out) const
{
    const ModuleHost& host = m_impl->modules;
    for (u32 i = 0; i < host.count(); ++i)
    {
        const ModuleInfo& info = host.module(i);
        const std::u8string path = info.path.u8string();
        PlayModuleInfo& module = out.emplace_back(PlayModuleInfo{info.name,
                                                                 info.version.toString(),
                                                                 std::string(path.begin(), path.end()),
                                                                 info.linkage == ModuleLinkage::Shared,
                                                                 info.reloads,
                                                                 {}});
        for (const ComponentMigration& migration : info.migration.types)
        {
            module.migrated.push_back(PlayComponentMigration{migration.type, migration.components,
                                                             migration.addedFields, migration.removedFields,
                                                             migration.resetFields});
        }
    }
}

} // namespace illusion::editor
