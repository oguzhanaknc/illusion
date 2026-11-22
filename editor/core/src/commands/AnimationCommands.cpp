// The animation commands (ADR 0059-0061): the skins and clips of a model (animation.list), what the Animators
// of the scene or the running game play (animation.status), driving a graph of the running game
// (animation.set_parameter, animation.trigger), setting the time of an Animator in the open scene
// (animation.scrub, undoable) and checking `.animgraph` and `.ragdoll` files (animation.validate). The
// runtime ones go through the AnimationWorld of the PlayHost and act on the play World; they are runtime
// state, not undoable.

#include "CommandSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationService.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/physics/Ragdoll.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

namespace strings = illusion::editor::strings;

constexpr usize kMaxStatusAnimators = 100;
constexpr usize kMaxStatusEvents = 64;
constexpr f64 kMaxTime = 1.0e6;
constexpr i64 kMaxIntParameter = 16777216; // what the graph format stores exactly in a float

std::string_view parameterTypeName(AnimationParameterType type) noexcept
{
    switch (type)
    {
    case AnimationParameterType::Float:
        return "Float";
    case AnimationParameterType::Int:
        return "Int";
    case AnimationParameterType::Bool:
        return "Bool";
    case AnimationParameterType::Trigger:
        return "Trigger";
    }
    return "Float";
}

Expected<AnimationWorld*, CommandError> requireAnimation(CommandContext& context)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    AnimationWorld* animation = context.onPlayWorld() ? play->animation() : nullptr;
    if (animation == nullptr)
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed,
            "animation graphs run in a play session: start one with play.start (the edit World has none)"));
    }
    return animation;
}

CommandError animationFailure(AnimationError error, std::string_view pointer)
{
    switch (error)
    {
    case AnimationError::NoAnimator:
        return CommandError::invalid(std::string(pointer), "has no illusion.Animator");
    case AnimationError::NoGraph:
        return CommandError::make(ErrorKind::PreconditionFailed, "the graph of the Animator has not loaded");
    case AnimationError::HasGraph:
        return CommandError::invalid(std::string(pointer), "is controlled by an animation graph");
    case AnimationError::UnknownName:
        return CommandError::invalid(std::string(pointer), "is not known to the Animator");
    case AnimationError::TypeMismatch:
        return CommandError::invalid(std::string(pointer), "does not fit the type of the parameter");
    case AnimationError::InvalidValue:
        return CommandError::invalid(std::string(pointer), "is not a valid value");
    }
    return CommandError::make(ErrorKind::Internal, std::string(animationErrorName(error)));
}

EntitySummary summaryOf(CommandContext& context, Entity entity)
{
    return context.world().isAlive(entity) ? context.summary(entity) : EntitySummary{};
}

// The Animator of @p entity named at @p pointer of the params.
Expected<Entity, CommandError> animatorEntity(CommandContext& context, std::string_view reference,
                                              std::string_view pointer)
{
    Expected<Entity, CommandError> entity = context.entity(reference, pointer);
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    if (!context.world().has<Animator>(*entity))
    {
        return unexpected(animationFailure(AnimationError::NoAnimator, pointer));
    }
    return entity;
}

// The first part of a validation error, "layers[0].states[1]: message".
std::string errorPathOf(const std::string& error)
{
    const usize colon = error.find(": ");
    if (colon != std::string::npos && error.find(' ') > colon)
    {
        return error.substr(0, colon);
    }
    return {};
}

// True if the file name of @p path is more than @p extension.
bool hasStem(std::string_view path, std::string_view extension)
{
    const usize slash = path.rfind('/');
    return slash != std::string_view::npos && path.size() > slash + 1 + extension.size();
}

std::string pathOf(const AssetRef& ref)
{
    return std::string(ref.path());
}

Expected<AnimationListResult, CommandError> animationList(CommandContext& context,
                                                          const AnimationListParams& params)
{
    const std::string extension = lowerExtension(params.path);
    std::filesystem::path real;
    if (!params.path.starts_with(kAssetsRoot) || (extension != ".gltf" && extension != ".glb") ||
        !hasStem(params.path, extension) || params.path.find('#') != std::string::npos ||
        context.editor().files().resolve(params.path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path",
            "must be a model (.gltf or .glb) in the assets: mount, e.g. \"assets:/models/hero.glb\""));
    }
    if (!context.editor().files().exists(params.path))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.path + "' does not exist"));
    }
    std::string error;
    const std::optional<ModelData> model = loadGltf(context.editor().files(), params.path, &error);
    if (!model)
    {
        return unexpected(
            CommandError::make(ErrorKind::IoError, "cannot read '" + params.path + "' (" + error + ")"));
    }
    AnimationListResult result;
    result.path = params.path;
    for (usize i = 0; i < model->skins.size(); ++i)
    {
        const Skeleton& skin = model->skins[i];
        result.skins.push_back(AnimationListSkin{.ref = std::format("{}#skin/{}", params.path, i),
                                                 .name = skin.name,
                                                 .joints = static_cast<u32>(skin.joints.size())});
    }
    for (const AnimationClip& clip : model->animations)
    {
        result.clips.push_back(
            AnimationListClip{.ref = std::format("{}#animation/{}", params.path, clip.name),
                              .name = clip.name,
                              .duration = clip.duration,
                              .channels = static_cast<u32>(clip.channels.size())});
    }
    return result;
}

AnimationStatusParameter parameterOf(const AnimationGraph& graph, const AnimationGraphState& state,
                                     usize index)
{
    const AnimationParameter& parameter = graph.parameters[index];
    f32 value = 0.0f;
    if (parameter.type == AnimationParameterType::Trigger)
    {
        value = index < state.triggers.size() && state.triggers[index] == 1 ? 1.0f : 0.0f;
    }
    else if (index < state.parameters.size())
    {
        value = state.parameters[index];
    }
    return AnimationStatusParameter{
        .name = parameter.name, .type = std::string(parameterTypeName(parameter.type)), .value = value};
}

AnimationStatusAnimator animatorOf(CommandContext& context, const AnimationWorld* animation, Entity entity,
                                   const Animator& animator)
{
    AnimationStatusAnimator status;
    status.entity = context.summary(entity);
    status.skeleton = pathOf(animator.skeleton);
    status.clip = pathOf(animator.clip);
    status.graph = pathOf(animator.graph);
    status.speed = animator.speed;
    status.time = animator.time;
    status.loop = animator.loop;
    status.playing = animator.playing;
    if (animation == nullptr)
    {
        return status;
    }
    if (const AnimationPose* pose = context.world().tryGet<AnimationPose>(entity))
    {
        status.joints = pose->jointCount;
        status.posed = pose->jointCount != 0;
    }
    const std::optional<u32> slot = animation->findSlot(entity);
    const AnimationGraph* graph = slot ? animation->graph(*slot) : nullptr;
    const AnimationGraphState* state = slot ? animation->graphState(*slot) : nullptr;
    if (graph == nullptr || state == nullptr)
    {
        return status;
    }
    status.graphLoaded = true;
    for (usize i = 0; i < graph->layers.size() && i < state->layers.size(); ++i)
    {
        const AnimationLayer& layer = graph->layers[i];
        const GraphLayerState& live = state->layers[i];
        AnimationStatusLayer entry;
        entry.name = layer.name;
        if (live.state < layer.states.size())
        {
            entry.state = layer.states[live.state].name;
        }
        entry.normalizedTime = live.time;
        if (live.previous >= 0 && static_cast<usize>(live.previous) < layer.states.size())
        {
            entry.previous = layer.states[static_cast<usize>(live.previous)].name;
        }
        if (live.fadeDuration > 0.0f)
        {
            entry.fade = std::clamp(live.fade / live.fadeDuration, 0.0f, 1.0f);
        }
        status.layers.push_back(std::move(entry));
    }
    for (usize i = 0; i < graph->parameters.size(); ++i)
    {
        status.parameters.push_back(parameterOf(*graph, *state, i));
    }
    return status;
}

Expected<AnimationStatusResult, CommandError> animationStatus(CommandContext& context,
                                                              const AnimationStatusParams& params)
{
    World& world = context.world();
    AnimationWorld* animation = nullptr;
    if (context.onPlayWorld())
    {
        if (PlayHost* play = context.editor().play())
        {
            animation = play->animation();
        }
    }
    std::optional<Entity> one;
    if (params.entity)
    {
        Expected<Entity, CommandError> entity = animatorEntity(context, *params.entity, "/entity");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        one = *entity;
    }
    std::vector<Entity> entities;
    if (one)
    {
        entities.push_back(*one);
    }
    else
    {
        world.query<const Animator>().each(
            [&](Entity entity, const Animator&) { entities.push_back(entity); });
        std::ranges::sort(entities, [](Entity a, Entity b) { return a.toBits() < b.toBits(); });
    }
    AnimationStatusResult result;
    result.world = context.onPlayWorld() ? "play" : "edit";
    result.live = animation != nullptr;
    if (!result.live)
    {
        result.note =
            context.onPlayWorld()
                ? "the play session has no animation runtime: only the saved Animator fields are shown"
                : "the edit World has no animation runtime here: only the saved Animator fields are "
                  "shown; start a play session (play.start) for poses, states and parameters";
    }
    {
        u32 total = 0;
        world.query<const Animator>().each([&](Entity, const Animator&) { ++total; });
        result.total = total;
    }
    for (const Entity entity : entities)
    {
        if (result.animators.size() >= kMaxStatusAnimators)
        {
            break;
        }
        if (const Animator* animator = world.tryGet<Animator>(entity))
        {
            result.animators.push_back(animatorOf(context, animation, entity, *animator));
        }
    }
    if (animation != nullptr)
    {
        const AnimationStatistics& statistics = animation->statistics();
        result.pending = statistics.pending;
        result.failures = statistics.failures;
        for (const AnimationEvent& event : animation->events())
        {
            if (result.events.size() >= kMaxStatusEvents)
            {
                break;
            }
            result.events.push_back(AnimationStatusEvent{.entity = summaryOf(context, event.entity),
                                                         .name = std::string(event.name),
                                                         .layer = std::string(event.layer),
                                                         .state = std::string(event.state)});
        }
    }
    return result;
}

// The graph of @p entity's Animator in @p animation, or the error the commands report.
struct LiveGraph
{
    const AnimationGraph* graph = nullptr;
    const AnimationGraphState* state = nullptr;
};

Expected<LiveGraph, CommandError> liveGraph(CommandContext& context, AnimationWorld& animation, Entity entity)
{
    const Animator* animator = context.world().tryGet<Animator>(entity);
    if (animator == nullptr || animator->graph.empty())
    {
        return unexpected(
            CommandError::invalid("/entity", "has no animation graph (Animator::graph is empty)"));
    }
    const std::optional<u32> slot = animation.findSlot(entity);
    const AnimationGraph* graph = slot ? animation.graph(*slot) : nullptr;
    const AnimationGraphState* state = slot ? animation.graphState(*slot) : nullptr;
    if (graph == nullptr || state == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the graph of the Animator has not loaded: step the game and "
                                             "check animation.status (graphLoaded)"));
    }
    return LiveGraph{graph, state};
}

// The index of the parameter @p name in @p graph, or the error with the names that are close.
Expected<usize, CommandError> findParameter(const AnimationGraph& graph, std::string_view name)
{
    const i32 index = graph.findParameter(name);
    if (index >= 0)
    {
        return static_cast<usize>(index);
    }
    std::vector<std::string_view> names;
    for (const AnimationParameter& parameter : graph.parameters)
    {
        names.push_back(parameter.name);
    }
    CommandError error = CommandError::invalid("/name", "is not a parameter of the graph");
    error.kind = ErrorKind::NotFound;
    error.suggestions = closestMatches(name, names);
    return unexpected(std::move(error));
}

Expected<AnimationSetParameterResult, CommandError>
animationSetParameter(CommandContext& context, const AnimationSetParameterParams& params)
{
    Expected<AnimationWorld*, CommandError> found = requireAnimation(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    AnimationWorld& animation = **found;
    Expected<Entity, CommandError> entity = animatorEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<JsonDocument, CommandError> json = parseParam(params.value, "/value");
    if (!json)
    {
        return unexpected(std::move(json).error());
    }
    Expected<LiveGraph, CommandError> live = liveGraph(context, animation, *entity);
    if (!live)
    {
        return unexpected(std::move(live).error());
    }
    Expected<usize, CommandError> index = findParameter(*live->graph, params.name);
    if (!index)
    {
        return unexpected(std::move(index).error());
    }
    const AnimationParameterType type = live->graph->parameters[*index].type;
    const JsonValue value = json->root();
    AnimationWorldService service(animation);
    Expected<void, AnimationError> written;
    f32 number = 0.0f;
    switch (type)
    {
    case AnimationParameterType::Trigger:
        return unexpected(CommandError::invalid("/name", "is a Trigger: use animation.trigger"));
    case AnimationParameterType::Bool:
    {
        const std::optional<bool> flag = value.asBool();
        if (!flag)
        {
            return unexpected(CommandError::invalid("/value", "must be true or false for a Bool parameter"));
        }
        number = *flag ? 1.0f : 0.0f;
        if (!context.dryRun())
        {
            written = service.setBool(*entity, params.name, *flag);
        }
        break;
    }
    case AnimationParameterType::Int:
    {
        const std::optional<i64> whole = value.asI64();
        if (!whole || *whole < -kMaxIntParameter || *whole > kMaxIntParameter)
        {
            return unexpected(CommandError::invalid(
                "/value", "must be a whole number between -16777216 and 16777216 for an Int parameter"));
        }
        number = static_cast<f32>(*whole);
        if (!context.dryRun())
        {
            written = service.setInt(*entity, params.name, static_cast<i32>(*whole));
        }
        break;
    }
    case AnimationParameterType::Float:
    {
        const std::optional<f64> real = value.asF64();
        if (!real || !std::isfinite(*real) ||
            std::abs(*real) > static_cast<f64>(std::numeric_limits<f32>::max()))
        {
            return unexpected(
                CommandError::invalid("/value", "must be a finite number for a Float parameter"));
        }
        number = static_cast<f32>(*real);
        if (!context.dryRun())
        {
            written = service.setFloat(*entity, params.name, number);
        }
        break;
    }
    }
    if (!written)
    {
        return unexpected(animationFailure(written.error(), "/name"));
    }
    return AnimationSetParameterResult{
        .entity = context.summary(*entity),
        .parameter = AnimationStatusParameter{
            .name = params.name, .type = std::string(parameterTypeName(type)), .value = number}};
}

Expected<AnimationTriggerResult, CommandError> animationTrigger(CommandContext& context,
                                                                const AnimationTriggerParams& params)
{
    Expected<AnimationWorld*, CommandError> found = requireAnimation(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    AnimationWorld& animation = **found;
    Expected<Entity, CommandError> entity = animatorEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<LiveGraph, CommandError> live = liveGraph(context, animation, *entity);
    if (!live)
    {
        return unexpected(std::move(live).error());
    }
    Expected<usize, CommandError> index = findParameter(*live->graph, params.name);
    if (!index)
    {
        return unexpected(std::move(index).error());
    }
    if (live->graph->parameters[*index].type != AnimationParameterType::Trigger)
    {
        return unexpected(CommandError::invalid("/name", "is not a Trigger: use animation.set_parameter"));
    }
    if (!context.dryRun())
    {
        AnimationWorldService service(animation);
        if (const Expected<void, AnimationError> set = service.setTrigger(*entity, params.name); !set)
        {
            return unexpected(animationFailure(set.error(), "/name"));
        }
    }
    return AnimationTriggerResult{.entity = context.summary(*entity), .name = params.name};
}

Expected<AnimationScrubResult, CommandError> animationScrub(CommandContext& context,
                                                            const AnimationScrubParams& params)
{
    if (!std::isfinite(params.time) || params.time < 0.0f || static_cast<f64>(params.time) > kMaxTime)
    {
        return unexpected(CommandError::invalid("/time", "must be between 0 and 1,000,000 seconds"));
    }
    Expected<Entity, CommandError> entity = animatorEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<ComponentId, CommandError> type = context.componentType("illusion.Animator", "/entity");
    if (!type)
    {
        return unexpected(std::move(type).error());
    }
    Animator animator = *context.world().tryGet<Animator>(*entity);
    const f32 previous = animator.time;
    animator.time = params.time;
    Expected<bool, CommandError> changed = context.edit().assignComponent(*entity, *type, &animator);
    if (!changed)
    {
        return unexpected(std::move(changed).error());
    }
    return AnimationScrubResult{
        .entity = context.summary(*entity), .previous = previous, .time = params.time, .changed = *changed};
}

Expected<AnimationValidateResult, CommandError> animationValidate(CommandContext& context,
                                                                  const AnimationValidateParams& params)
{
    const std::string extension = lowerExtension(params.path);
    const bool graph = extension == kAnimationGraphExtension;
    const bool ragdoll = extension == kRagdollProfileExtension;
    std::filesystem::path real;
    if (!params.path.starts_with(kAssetsRoot) || (!graph && !ragdoll) || !hasStem(params.path, extension) ||
        context.editor().files().resolve(params.path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path", "must be an .animgraph or .ragdoll file in the assets: mount, e.g. "
                     "\"assets:/animations/hero.animgraph\""));
    }
    std::vector<std::byte> bytes;
    const FileError error = context.editor().files().readFile(params.path, bytes);
    if (error == FileError::NotFound)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.path + "' does not exist"));
    }
    if (error != FileError::None)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, "cannot read '" + params.path + "' (" +
                                                                     std::string(fileErrorName(error)) +
                                                                     ")"));
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    AnimationValidateResult result;
    result.path = params.path;
    if (graph)
    {
        result.kind = "AnimationGraph";
        const Expected<AnimationGraph, std::string> parsed = parseAnimationGraph(text);
        if (!parsed)
        {
            result.error = parsed.error();
            result.errorPath = errorPathOf(result.error);
            return result;
        }
        result.valid = true;
        result.parameters = static_cast<u32>(parsed->parameters.size());
        result.layers = static_cast<u32>(parsed->layers.size());
        for (const AnimationLayer& layer : parsed->layers)
        {
            result.states += static_cast<u32>(layer.states.size());
        }
        return result;
    }
    result.kind = "RagdollProfile";
    const Expected<RagdollProfile, std::string> parsed = parseRagdollProfile(text);
    if (!parsed)
    {
        result.error = parsed.error();
        result.errorPath = errorPathOf(result.error);
        return result;
    }
    result.valid = true;
    result.parts = static_cast<u32>(parsed->parts.size());
    return result;
}

} // namespace

void registerAnimationCommands(CommandRegistry& registry)
{
    registry.add<&animationList>({
        .name = "animation.list",
        .title = strings::kCommandAnimationListTitle,
        .description = strings::kCommandAnimationListDescription,
        .agentSummary =
            "Lists what a glTF model of the project can animate: its skins (the value for "
            "Animator::skeleton, "
            "with the number of joints) and its animations (the value for Animator::clip, with the duration "
            "in seconds and the number of animated channels). Use it to find the names for an Animator or an "
            "animation graph. Example: {\"path\": \"assets:/models/column.gltf\"}.",
        .category = "Animation",
        .icon = "animation",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
    registry.add<&animationStatus>({
        .name = "animation.status",
        .title = strings::kCommandAnimationStatusTitle,
        .description = strings::kCommandAnimationStatusDescription,
        .agentSummary =
            "Reports the Animators of the scene (entity names one, otherwise up to 100): the saved clip, "
            "graph, speed, time, loop and playing, and, in a play session (play.start), whether the pose "
            "exists and its joint count, whether the graph has loaded, for each layer the current state, its "
            "normalized time and the state a running transition fades out, every parameter with its value "
            "(a Trigger is 1 while set), and the state events of the last update. Without a play session "
            "(world edit) only the saved fields are shown and note says so. Example: "
            "{\"entity\": \"/Column\"}.",
        .category = "Animation",
        .icon = "animation",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&animationSetParameter>({
        .name = "animation.set_parameter",
        .title = strings::kCommandAnimationSetParameterTitle,
        .description = strings::kCommandAnimationSetParameterDescription,
        .agentSummary =
            "Sets a Float, Int or Bool parameter of the animation graph of an Animator in the running play "
            "session; the graph uses it at the next update (play.step), where animation.status shows it. "
            "value is a number for a Float, a whole number up to 16777216 for an Int, true or false for a "
            "Bool; a Trigger takes animation.trigger. Not undoable: it is runtime state. Example: "
            "{\"entity\": \"/Column\", \"name\": \"lean\", \"value\": 0.8}.",
        .category = "Animation",
        .icon = "tune",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&animationTrigger>({
        .name = "animation.trigger",
        .title = strings::kCommandAnimationTriggerTitle,
        .description = strings::kCommandAnimationTriggerDescription,
        .agentSummary =
            "Sets a Trigger of the animation graph of an Animator in the running play session: it stays set "
            "until a transition consumes it or the next update ends (play.step). Not undoable: it is "
            "runtime state. Use animation.status for the parameters. Example: {\"entity\": \"/Column\", "
            "\"name\": \"bow\"}.",
        .category = "Animation",
        .icon = "bolt",
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&animationScrub>({
        .name = "animation.scrub",
        .title = strings::kCommandAnimationScrubTitle,
        .description = strings::kCommandAnimationScrubDescription,
        .agentSummary =
            "Sets Animator::time, the seconds into the clip (or into the entry states of the graph), of an "
            "Animator of the open scene; the viewport shows the pose at that time. Undoable and saved with "
            "the scene; the game starts from it. time is between 0 and 1,000,000; a looping clip wraps, a "
            "clip that does not loop holds its last pose. The result has the time before. Example: "
            "{\"entity\": \"/Column\", \"time\": 0.6}.",
        .category = "Animation",
        .icon = "timeline",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Edit,
    });
    registry.add<&animationValidate>({
        .name = "animation.validate",
        .title = strings::kCommandAnimationValidateTitle,
        .description = strings::kCommandAnimationValidateDescription,
        .agentSummary =
            "Checks an .animgraph or .ragdoll file of the project: valid is true when it parses (then "
            "parameters, layers and states count a graph, parts a ragdoll profile). Otherwise error is the "
            "first problem and errorPath the JSON path of the offending value, e.g. "
            "\"layers[0].states[2].motion\". A file that does not parse is a result, not a failed command. "
            "Use it after writing a graph with asset tools. Example: {\"path\": "
            "\"assets:/animations/column.animgraph\"}.",
        .category = "Animation",
        .icon = "fact_check",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
}

} // namespace illusion::editor::detail
