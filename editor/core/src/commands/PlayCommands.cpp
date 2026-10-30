#include "CommandSupport.hpp"

#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>

#include <algorithm>
#include <cmath>

namespace illusion::editor
{

std::string_view playStateName(PlayState state) noexcept
{
    switch (state)
    {
    case PlayState::Stopped:
        return "stopped";
    case PlayState::Playing:
        return "playing";
    case PlayState::Paused:
        return "paused";
    }
    return "stopped";
}

std::string_view playModeName(PlayMode mode) noexcept
{
    return mode == PlayMode::Simulate ? "simulate" : "play";
}

} // namespace illusion::editor

namespace illusion::editor::detail
{
namespace
{

constexpr u32 kMaxSteps = 100'000;

Expected<PlayHost*, CommandError> requirePlay(CommandContext& context)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    return play;
}

PlayStatusResult statusResult(const PlayStatus& status)
{
    return PlayStatusResult{std::string(playStateName(status.state)),
                            std::string(playModeName(status.mode)),
                            status.fixedStep,
                            status.timeScale,
                            static_cast<u32>(std::min<u64>(status.steps, ~u32{0})),
                            status.time};
}

Expected<PlayStatusResult, CommandError> playStart(CommandContext& context, const PlayStartParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    PlayStartOptions options;
    if (params.mode)
    {
        if (*params.mode == "simulate")
        {
            options.mode = PlayMode::Simulate;
        }
        else if (*params.mode != "play")
        {
            return unexpected(CommandError::invalid("/mode", "must be \"play\" or \"simulate\""));
        }
    }
    options.fixedStep = params.fixedStep.value_or(false);
    options.seed = params.seed.value_or(0);
    if (context.editor().playing())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "a play session runs already; stop it with play.stop"));
    }
    if (!context.dryRun())
    {
        if (Expected<void, CommandError> started = (*play)->start(options); !started)
        {
            return unexpected(std::move(started).error());
        }
    }
    return statusResult((*play)->status());
}

Expected<PlayStatusResult, CommandError> playStop(CommandContext& context, const NoParams&)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    if (!context.dryRun())
    {
        (*play)->stop();
    }
    return statusResult((*play)->status());
}

Expected<PlayStatusResult, CommandError> playPause(CommandContext& context, const PlayPauseParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    if (!context.dryRun())
    {
        (*play)->setPaused(params.paused);
    }
    return statusResult((*play)->status());
}

Expected<PlayStatusResult, CommandError> playStep(CommandContext& context, const PlayStepParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    const u32 steps = params.steps.value_or(1);
    if (steps == 0 || steps > kMaxSteps)
    {
        return unexpected(CommandError::invalid("/steps", "must be between 1 and 100000"));
    }
    if (!context.dryRun())
    {
        static_cast<void>((*play)->step(steps));
    }
    return statusResult((*play)->status());
}

Expected<PlayStatusResult, CommandError> playSetTimeScale(CommandContext& context,
                                                          const PlayTimeScaleParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    if (!std::isfinite(params.scale) || params.scale < 0.0 || params.scale > 100.0)
    {
        return unexpected(CommandError::invalid("/scale", "must be between 0 and 100"));
    }
    if (!context.dryRun())
    {
        (*play)->setTimeScale(params.scale);
    }
    return statusResult((*play)->status());
}

Expected<PlayStatusResult, CommandError> playStatus(CommandContext& context, const NoParams&)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    return statusResult((*play)->status());
}

// Deep comparison of two JSON values; integers and reals compare by value.
bool jsonEqual(JsonValue a, JsonValue b)
{
    if (a.isNumber() && b.isNumber())
    {
        return a.asF64() == b.asF64();
    }
    if (a.type() != b.type())
    {
        return false;
    }
    switch (a.type())
    {
    case JsonType::Bool:
        return a.asBool() == b.asBool();
    case JsonType::String:
        return a.asString() == b.asString();
    case JsonType::Array:
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (usize i = 0; i < a.size(); ++i)
        {
            if (!jsonEqual(a[i], b[i]))
            {
                return false;
            }
        }
        return true;
    }
    case JsonType::Object:
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (const JsonMember member : a.members())
        {
            const JsonValue other = b.find(member.key);
            if (other.isAbsent() || !jsonEqual(member.value, other))
            {
                return false;
            }
        }
        return true;
    }
    default:
        return true;
    }
}

// The condition of play.run_until, checked against the play World.
class RunCondition
{
public:
    static Expected<RunCondition, CommandError> make(CommandContext& context,
                                                     const PlayRunUntilParams& params)
    {
        RunCondition condition;
        condition.m_entity = params.entity;
        if (params.exists)
        {
            if (params.component || params.field || params.equals)
            {
                return unexpected(
                    CommandError::invalid("/exists", "cannot be combined with component, field or equals"));
            }
            condition.m_exists = params.exists;
            return condition;
        }
        if (!params.component || !params.equals)
        {
            return unexpected(CommandError::invalid(
                "/component", "is required with equals unless exists is given, e.g. {\"exists\": true}"));
        }
        Expected<ComponentId, CommandError> type = context.componentType(*params.component, "/component");
        if (!type)
        {
            return unexpected(std::move(type).error());
        }
        condition.m_type = *type;
        condition.m_field = params.field.value_or("");
        if (!condition.m_field.empty() && condition.m_field.front() != '/')
        {
            return unexpected(CommandError::invalid("/field", "must be a JSON Pointer such as \"/lines\""));
        }
        Expected<JsonDocument, CommandError> equals = parseParam(*params.equals, "/equals");
        if (!equals)
        {
            return unexpected(std::move(equals).error());
        }
        condition.m_equals = std::move(equals).value();
        return condition;
    }

    Expected<bool, CommandError> met(EditorContext& editor) const
    {
        EntityIndex* index = editor.playEntities();
        if (index == nullptr)
        {
            return false;
        }
        Expected<Entity, CommandError> entity = index->resolve(m_entity, editor.selection(), "/entity");
        if (!entity && entity.error().kind != ErrorKind::NotFound)
        {
            return unexpected(std::move(entity).error());
        }
        if (m_exists)
        {
            return entity.hasValue() == *m_exists;
        }
        if (!entity)
        {
            return false;
        }
        const std::optional<std::string> captured = editor.playEditor()->capture(*entity, m_type);
        if (!captured)
        {
            return false;
        }
        Expected<JsonDocument, JsonError> value = JsonDocument::parse(*captured);
        if (!value)
        {
            return false;
        }
        const JsonValue field = m_field.empty() ? value->root() : value->root().pointer(m_field);
        return !field.isAbsent() && jsonEqual(field, m_equals->root());
    }

private:
    std::string m_entity;
    std::optional<bool> m_exists;
    ComponentId m_type = kInvalidComponentId;
    std::string m_field;
    std::optional<JsonDocument> m_equals;
};

Expected<PlayRunUntilResult, CommandError> playRunUntil(CommandContext& context,
                                                        const PlayRunUntilParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    if (params.maxSteps == 0 || params.maxSteps > kMaxSteps)
    {
        return unexpected(CommandError::invalid("/maxSteps", "must be between 1 and 100000"));
    }
    Expected<RunCondition, CommandError> condition = RunCondition::make(context, params);
    if (!condition)
    {
        return unexpected(std::move(condition).error());
    }
    PlayRunUntilResult result;
    Expected<bool, CommandError> met = condition->met(context.editor());
    while (met && !*met && result.steps < params.maxSteps && !context.dryRun())
    {
        if ((*play)->step(1) == 0)
        {
            break;
        }
        ++result.steps;
        met = condition->met(context.editor());
    }
    if (!met)
    {
        return unexpected(std::move(met).error());
    }
    result.met = *met;
    result.status = statusResult((*play)->status());
    return result;
}

// Components that play.keep_changes never copies.
bool keepable(const TypeReflection& reflection, ComponentId type, bool child)
{
    if (reflection.meta.transient || type == componentId<EntityGuid>() || type == componentId<Hierarchy>())
    {
        return false;
    }
    // A child's Transform is derived from its LocalTransform; a root's LocalTransform from its Transform.
    return type != (child ? componentId<Transform>() : componentId<LocalTransform>());
}

Expected<PlayKeepChangesResult, CommandError> playKeepChanges(CommandContext& context,
                                                              const PlayKeepChangesParams& params)
{
    EditorContext& editor = context.editor();
    EntityIndex* playIndex = editor.playEntities();
    if (playIndex == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::WrongMode, "there is no play World"));
    }
    if (params.entities.empty())
    {
        return unexpected(CommandError::invalid("/entities", "must name at least one entity"));
    }
    std::vector<Entity> sources;
    if (Expected<void, CommandError> resolved =
            playIndex->resolveAll(params.entities, editor.selection(), "/entities", sources);
        !resolved)
    {
        return unexpected(std::move(resolved).error());
    }
    std::vector<ComponentId> only;
    if (params.components)
    {
        for (usize i = 0; i < params.components->size(); ++i)
        {
            Expected<ComponentId, CommandError> type =
                context.componentType((*params.components)[i], "/components/" + std::to_string(i));
            if (!type)
            {
                return unexpected(std::move(type).error());
            }
            only.push_back(*type);
        }
    }
    World& playWorld = playIndex->world();
    const ComponentRegistry& registry = playWorld.registry();
    WorldEditor& playEditor = *editor.playEditor();
    PlayKeepChangesResult result;
    std::vector<ComponentId> types;
    for (const Entity source : sources)
    {
        const EntityGuid guid = playIndex->guidOf(source);
        const Entity target = guid.isNull() ? Entity{} : editor.entities().find(guid);
        if (target.isNull())
        {
            context.warn("'" + playIndex->pathOf(source) +
                         "' does not exist in the edit World (it was spawned while playing)");
            continue;
        }
        const std::span<const ComponentId> sourceTypes = playWorld.componentsOf(source);
        types.assign(sourceTypes.begin(), sourceTypes.end());
        const Hierarchy* hierarchy = playWorld.tryGet<Hierarchy>(source);
        const bool child = hierarchy != nullptr && !hierarchy->parent.isNull();
        u32 copied = 0;
        for (const ComponentId type : types)
        {
            const TypeReflection* reflection = registry.reflection(type);
            if (reflection == nullptr || !keepable(*reflection, type, child) ||
                (!only.empty() && std::find(only.begin(), only.end(), type) == only.end()))
            {
                continue;
            }
            const std::optional<std::string> captured = playEditor.capture(source, type);
            if (!captured)
            {
                continue;
            }
            Expected<JsonDocument, JsonError> value = JsonDocument::parse(*captured);
            if (!value)
            {
                continue;
            }
            const std::span<const ComponentId> targetTypes = context.world().componentsOf(target);
            if (std::find(targetTypes.begin(), targetTypes.end(), type) != targetTypes.end())
            {
                Expected<bool, CommandError> set =
                    context.edit().setComponent(target, type, value->root(), JsonReadMode::Replace);
                if (!set)
                {
                    return unexpected(std::move(set).error());
                }
            }
            else
            {
                Expected<std::vector<std::string>, CommandError> added =
                    context.edit().addComponent(target, type, value->root());
                if (!added)
                {
                    return unexpected(std::move(added).error());
                }
            }
            ++copied;
        }
        result.components += copied;
        ++result.entities;
    }
    return result;
}

Expected<ModuleListResult, CommandError> moduleListOf(PlayHost& play)
{
    ModuleListResult result;
    play.modules(result.modules);
    return result;
}

Expected<ModuleListResult, CommandError> moduleLoad(CommandContext& context, const ModuleLoadParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    if (params.path.empty())
    {
        return unexpected(CommandError::invalid("/path", "must name a module library"));
    }
    if (!context.dryRun())
    {
        if (Expected<std::string, CommandError> loaded = (*play)->loadModule(params.path); !loaded)
        {
            return unexpected(std::move(loaded).error());
        }
    }
    return moduleListOf(**play);
}

Expected<ModuleListResult, CommandError> moduleUnload(CommandContext& context,
                                                      const ModuleUnloadParams& params)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    std::vector<PlayModuleInfo> modules;
    (*play)->modules(modules);
    if (std::none_of(modules.begin(), modules.end(),
                     [&](const PlayModuleInfo& module) { return module.name == params.name; }))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "no module '" + params.name + "' is loaded"));
    }
    if (!context.dryRun())
    {
        static_cast<void>((*play)->unloadModule(params.name));
    }
    return moduleListOf(**play);
}

Expected<ModuleListResult, CommandError> moduleList(CommandContext& context, const NoParams&)
{
    Expected<PlayHost*, CommandError> play = requirePlay(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    return moduleListOf(**play);
}

} // namespace

void registerPlayCommands(CommandRegistry& registry)
{
    registry.add<&playStart>({
        .name = "play.start",
        .title = strings::kCommandPlayStartTitle,
        .description = strings::kCommandPlayStartDescription,
        .agentSummary =
            "Starts a play session (ADR 0042): copies the edit World into a play World and runs the game "
            "on it each frame. While it runs, entity, component, transform and hierarchy commands act on "
            "the play World and are not undoable; play.stop throws it away. mode \"simulate\" runs physics "
            "only. fixedStep makes every frame one physics step with seeded ids, for repeatable runs. "
            "Example: {\"mode\": \"play\", \"fixedStep\": true}.",
        .category = "Play",
        .icon = "play_arrow",
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&playStop>({
        .name = "play.stop",
        .title = strings::kCommandPlayStopTitle,
        .description = strings::kCommandPlayStopDescription,
        .agentSummary = "Stops the play session and destroys the play World; the edit World is exactly as "
                        "before play.start. Keep values first with play.keep_changes. Example: {}.",
        .category = "Play",
        .icon = "stop",
        .flags = {.idempotent = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&playPause>({
        .name = "play.pause",
        .title = strings::kCommandPlayPauseTitle,
        .description = strings::kCommandPlayPauseDescription,
        .agentSummary =
            "Pauses (paused: true) or resumes (false) the play session; a paused session advances "
            "only through play.step. Example: {\"paused\": true}.",
        .category = "Play",
        .icon = "pause",
        .flags = {.idempotent = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&playStep>({
        .name = "play.step",
        .title = strings::kCommandPlayStepTitle,
        .description = strings::kCommandPlayStepDescription,
        .agentSummary =
            "Advances the play session by fixed physics steps at once (default 1), paused or not. "
            "Example: {\"steps\": 60}.",
        .category = "Play",
        .icon = "skip_next",
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&playSetTimeScale>({
        .name = "play.set_time_scale",
        .title = strings::kCommandPlaySetTimeScaleTitle,
        .description = strings::kCommandPlaySetTimeScaleDescription,
        .agentSummary = "Multiplies the frame time play sessions advance by: 0 freezes, 0.5 is slow motion; "
                        "kept across sessions, ignored by play.step. Example: {\"scale\": 0.5}.",
        .category = "Play",
        .icon = "speed",
        .flags = {.idempotent = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&playStatus>({
        .name = "play.status",
        .title = strings::kCommandPlayStatusTitle,
        .description = strings::kCommandPlayStatusDescription,
        .agentSummary = "Returns whether a play session runs (state stopped, playing or paused), its mode, "
                        "steps and game time. Example: {}.",
        .category = "Play",
        .icon = "info",
        .flags = {.readOnly = true, .idempotent = true},
    });
    registry.add<&playRunUntil>({
        .name = "play.run_until",
        .title = strings::kCommandPlayRunUntilTitle,
        .description = strings::kCommandPlayRunUntilDescription,
        .agentSummary =
            "Steps the play session until a condition holds on the play World or maxSteps pass, and "
            "reports whether it held: an entity exists (exists: true) or is gone (false), or a Component "
            "value (optionally the field at a JSON Pointer) equals a value. Example: {\"maxSteps\": 600, "
            "\"entity\": \"Board\", \"component\": \"game.Score\", \"field\": \"/lines\", \"equals\": 1}.",
        .category = "Play",
        .icon = "fast_forward",
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
    });
    registry.add<&playKeepChanges>({
        .name = "play.keep_changes",
        .title = strings::kCommandPlayKeepChangesTitle,
        .description = strings::kCommandPlayKeepChangesDescription,
        .agentSummary =
            "Copies the saved Component values of play World entities to the same entities (by id) of the "
            "edit World as one undoable step, all saved Components or only the named ones; entities "
            "spawned while playing are skipped with a warning. Example: {\"entities\": [\"Player\"], "
            "\"components\": [\"illusion.Transform\"]}.",
        .category = "Play",
        .icon = "save_as",
        .flags = {.undoable = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&moduleLoad>({
        .name = "module.load",
        .title = strings::kCommandModuleLoadTitle,
        .description = strings::kCommandModuleLoadDescription,
        .agentSummary =
            "Loads a game module from its shared library and starts it on a running play session; "
            "fails with code 1302 and the loader's message when it cannot. Returns the loaded "
            "modules. Example: {\"path\": \"build/modules/tetris.dll\"}.",
        .category = "Module",
        .icon = "extension",
        .permission = PermissionClass::Build,
    });
    registry.add<&moduleUnload>({
        .name = "module.unload",
        .title = strings::kCommandModuleUnloadTitle,
        .description = strings::kCommandModuleUnloadDescription,
        .agentSummary = "Stops and unloads a game module by name. Example: {\"name\": \"tetris\"}.",
        .category = "Module",
        .icon = "extension_off",
        .permission = PermissionClass::Build,
    });
    registry.add<&moduleList>({
        .name = "module.list",
        .title = strings::kCommandModuleListTitle,
        .description = strings::kCommandModuleListDescription,
        .agentSummary = "Lists the loaded game modules with version, library path and hot reload count. "
                        "Example: {}.",
        .category = "Module",
        .icon = "extension",
        .flags = {.readOnly = true, .idempotent = true},
    });
}

} // namespace illusion::editor::detail
