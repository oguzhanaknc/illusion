#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/SettingsCatalog.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/runtime/GameSession.hpp>

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

constexpr std::array<RuntimeSettingsFile, 4> kRuntimeSettings{{
    {"illusion.PhysicsSettings", kPhysicsSettingsFile},
    {"illusion.AudioSettings", kAudioSettingsFile},
    {"illusion.InputSettings", kInputSettingsFile},
    {"illusion.RenderSettings", kRenderSettingsFile},
}};

// Components whose changes need the physics body rebuilt.
constexpr std::array<std::string_view, 4> kPhysicsTypes{"illusion.RigidBody", "illusion.Collider",
                                                        "illusion.CharacterController", "illusion.Joint"};

constexpr std::string_view kAudioSource = "illusion.AudioSource";

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
    std::unique_ptr<GameSession> game;
    PlayStartOptions options;
    bool paused = false;
    f64 timeScale = 1.0;
};

PlaySession::PlaySession(EditorContext& editor, const PlaySessionDesc& desc)
{
    ILL_ASSERT(desc.jobs != nullptr, "a PlaySession needs a JobSystem");
    m_impl = std::make_unique<Impl>(editor, desc);
    registerRuntimeSettings(editor.settings());
    m_impl->modules.attachWorld(editor.world(), WorldRole::Edit);
    editor.setPlay(this);
}

PlaySession::~PlaySession()
{
    stop();
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
    desc.audioOutput = impl.desc.audioOutput;
    desc.fixedStep = options.fixedStep;
    desc.seed = options.seed;
    Expected<std::unique_ptr<GameSession>, std::string> game = GameSession::create(desc);
    if (!game)
    {
        return unexpected(CommandError::make(ErrorKind::ValidationFailed, game.error()));
    }
    // The snapshot keeps the ids, so commands and the selection find the same entities in both Worlds.
    Expected<std::string, CommandError> scene = impl.editor.sceneJson();
    if (!scene)
    {
        return unexpected(std::move(scene).error());
    }
    const SceneLoadResult loaded = loadScene((*game)->world(), *scene);
    if (!loaded.ok())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot copy the scene: " + loaded.error));
    }
    impl.game = std::move(game).value();
    impl.options = options;
    impl.paused = false;
    impl.editor.attachPlayWorld(impl.game->world());
    impl.game->start();
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
    const f64 stepSeconds = impl.game->settings().physics.stepSeconds;
    for (u32 i = 0; i < steps; ++i)
    {
        impl.game->update(stepSeconds);
    }
    impl.editor.playWorldAdvanced();
    return steps;
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
    impl.game->update(deltaSeconds * impl.timeScale);
    impl.editor.playWorldAdvanced();
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
        state.value =
            type == ActionType::Vector2 ? actions.vector(action) : Vec2(actions.value(action), 0.0f);
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
