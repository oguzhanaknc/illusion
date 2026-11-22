// The ai.* commands (ADR 0062): what the AI of the running game does (ai.status), writing a blackboard key
// (ai.setValue), making a noise (ai.noise) and checking a `.behavior` file (ai.validate). The first three go
// through the AiWorld of the PlayHost and act on the play World; they are runtime state, not undoable.

#include "CommandSupport.hpp"

#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorld.hpp>
#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/ai/BehaviorTreeState.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

namespace strings = illusion::editor::strings;

constexpr usize kMaxStatusAgents = 100;
constexpr f32 kMaxCoordinate = 1.0e6f;

Expected<AiWorld*, CommandError> requireAi(CommandContext& context)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    AiWorld* ai = context.onPlayWorld() ? play->ai() : nullptr;
    if (ai == nullptr)
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed,
            "the AI runs in a play session: start one with play.start (the edit World has no AI)"));
    }
    return ai;
}

CommandError aiFailure(AiError error, std::string_view pointer)
{
    switch (error)
    {
    case AiError::NoAgent:
        return CommandError::invalid(std::string(pointer),
                                     "has no illusion.BehaviorAgent or illusion.AiPerception");
    case AiError::NoTree:
        return CommandError::make(ErrorKind::PreconditionFailed,
                                  "the tree of the agent has not loaded (see ai.status and the editor log)");
    case AiError::UnknownKey:
        return CommandError::invalid(std::string(pointer), "is not a key of the agent's tree");
    case AiError::TypeMismatch:
        return CommandError::invalid(std::string(pointer), "does not fit the type of the key");
    case AiError::UnknownAction:
        return CommandError::invalid(std::string(pointer), "is not a running action");
    case AiError::InvalidValue:
        return CommandError::invalid(std::string(pointer), "is not a valid value");
    case AiError::Full:
        return CommandError::make(ErrorKind::PreconditionFailed,
                                  "the noises of this update are full; step the game and try again");
    }
    return CommandError::make(ErrorKind::Internal, std::string(aiErrorName(error)));
}

bool isAgent(const World& world, Entity entity)
{
    return world.isAlive(entity) && (world.has<BehaviorAgent>(entity) || world.has<AiPerception>(entity));
}

EntitySummary summaryOf(CommandContext& context, Entity entity)
{
    return context.world().isAlive(entity) ? context.summary(entity) : EntitySummary{};
}

std::string valueText(CommandContext& context, const BlackboardValue& value)
{
    switch (value.type)
    {
    case BlackboardType::Bool:
        return value.flag ? "true" : "false";
    case BlackboardType::Int:
        return std::to_string(value.integer);
    case BlackboardType::Float:
        return std::format("{}", value.number);
    case BlackboardType::Vector3:
        return value.set ? std::format("[{}, {}, {}]", value.vector.x, value.vector.y, value.vector.z)
                         : std::string();
    case BlackboardType::Entity:
        return value.set && context.world().isAlive(value.entity) ? context.summary(value.entity).path
                                                                  : std::string();
    }
    return {};
}

bool isSet(CommandContext& context, const BlackboardValue& value)
{
    return value.type == BlackboardType::Entity ? value.set && context.world().isAlive(value.entity)
                                                : value.set;
}

AiStatusKey keyOf(CommandContext& context, std::string_view name, const BlackboardValue& value)
{
    return AiStatusKey{.name = std::string(name),
                       .type = std::string(blackboardTypeName(value.type)),
                       .set = isSet(context, value),
                       .value = valueText(context, value)};
}

std::string_view senseName(AiSense sense) noexcept
{
    return sense == AiSense::Sight ? "Sight" : "Hearing";
}

AiStatusAgent agentOf(CommandContext& context, AiWorld& ai, Entity entity)
{
    World& world = context.world();
    AiStatusAgent agent;
    agent.entity = context.summary(entity);
    if (const BehaviorAgent* behavior = world.tryGet<BehaviorAgent>(entity))
    {
        agent.tree = std::string(behavior->tree.path());
    }
    const std::optional<u32> slot = ai.findSlot(entity);
    const BehaviorTree* tree = slot ? ai.tree(*slot) : nullptr;
    const BehaviorTreeState* state = slot ? ai.treeState(*slot) : nullptr;
    if (tree != nullptr && state != nullptr)
    {
        agent.loaded = true;
        agent.ticks = static_cast<u32>(std::min<u64>(state->ticks, std::numeric_limits<u32>::max()));
        const i32 running = runningNode(*tree, *state);
        if (running >= 0)
        {
            agent.running = tree->nodes[static_cast<usize>(running)].name;
        }
        for (usize i = 0; i < tree->blackboard.size() && i < state->blackboard.size(); ++i)
        {
            agent.blackboard.push_back(keyOf(context, tree->blackboard[i].name, state->blackboard[i]));
        }
    }
    std::array<AiPerceived, kMaxPerceived> perceived{};
    if (const Expected<u32, AiError> count = ai.perceived(entity, perceived))
    {
        for (u32 i = 0; i < *count && i < perceived.size(); ++i)
        {
            const AiPerceived& seen = perceived[i];
            agent.perceived.push_back(AiStatusPerceived{.entity = summaryOf(context, seen.entity),
                                                        .sense = std::string(senseName(seen.sense)),
                                                        .position = seen.position,
                                                        .age = seen.age,
                                                        .current = seen.current});
        }
    }
    return agent;
}

Expected<AiStatusResult, CommandError> aiStatus(CommandContext& context, const AiStatusParams& params)
{
    Expected<AiWorld*, CommandError> found = requireAi(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    AiWorld& ai = **found;
    World& world = context.world();
    std::optional<Entity> one;
    if (params.entity)
    {
        Expected<Entity, CommandError> entity = context.entity(*params.entity, "/entity");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        if (!isAgent(world, *entity))
        {
            return unexpected(aiFailure(AiError::NoAgent, "/entity"));
        }
        one = *entity;
    }
    if (params.wait.value_or(true))
    {
        ai.waitIdle();
    }
    std::vector<Entity> entities;
    if (one)
    {
        entities.push_back(*one);
    }
    else
    {
        world.query<const BehaviorAgent>().without<Inactive>().each(
            [&](Entity entity, const BehaviorAgent&) { entities.push_back(entity); });
        world.query<const AiPerception>().without<Inactive>().each([&](Entity entity, const AiPerception&) {
            if (!world.has<BehaviorAgent>(entity))
            {
                entities.push_back(entity);
            }
        });
        std::ranges::sort(entities, [](Entity a, Entity b) { return a.toBits() < b.toBits(); });
    }
    AiStatusResult result;
    result.total = static_cast<u32>(entities.size());
    for (const Entity entity : entities)
    {
        if (result.agents.size() >= kMaxStatusAgents)
        {
            break;
        }
        result.agents.push_back(agentOf(context, ai, entity));
    }
    const AiStatistics& statistics = ai.statistics();
    result.ticked = statistics.ticked;
    result.pending = statistics.pending;
    result.failures = statistics.failures;
    return result;
}

// The value for a key of @p type from the JSON of the params.
Expected<BlackboardValue, CommandError> parseValue(CommandContext& context, BlackboardType type,
                                                   const JsonValue& json)
{
    BlackboardValue value;
    value.type = type;
    const std::string_view typeName = blackboardTypeName(type);
    if (json.isNull())
    {
        if (type != BlackboardType::Vector3 && type != BlackboardType::Entity)
        {
            return unexpected(CommandError::invalid(
                "/value",
                std::format("null clears a Vector3 or Entity key; a {} key always has a value", typeName)));
        }
        return value;
    }
    value.set = true;
    switch (type)
    {
    case BlackboardType::Bool:
    {
        const std::optional<bool> flag = json.asBool();
        if (!flag)
        {
            return unexpected(CommandError::invalid("/value", "must be true or false for a Bool key"));
        }
        value.flag = *flag;
        return value;
    }
    case BlackboardType::Int:
    {
        const std::optional<i64> number = json.asI64();
        if (!number || *number < std::numeric_limits<i32>::min() || *number > std::numeric_limits<i32>::max())
        {
            return unexpected(
                CommandError::invalid("/value", "must be a whole number within 32 bits for an Int key"));
        }
        value.integer = static_cast<i32>(*number);
        return value;
    }
    case BlackboardType::Float:
    {
        const std::optional<f64> number = json.asF64();
        if (!number || !std::isfinite(*number) || std::abs(*number) > std::numeric_limits<f32>::max())
        {
            return unexpected(CommandError::invalid("/value", "must be a finite number for a Float key"));
        }
        value.number = static_cast<f32>(*number);
        return value;
    }
    case BlackboardType::Vector3:
    {
        std::array<f32, 3> parts{};
        if (!json.isArray() || json.size() != 3)
        {
            return unexpected(CommandError::invalid("/value", "must be [x, y, z] for a Vector3 key"));
        }
        for (usize i = 0; i < 3; ++i)
        {
            const std::optional<f64> number = json[i].asF64();
            if (!number || !std::isfinite(*number) || std::abs(*number) > static_cast<f64>(kMaxCoordinate))
            {
                return unexpected(CommandError::invalid(std::format("/value/{}", i),
                                                        "must be a finite coordinate within 1,000,000 m"));
            }
            parts[i] = static_cast<f32>(*number);
        }
        value.vector = Vec3(parts[0], parts[1], parts[2]);
        return value;
    }
    case BlackboardType::Entity:
    {
        const std::optional<std::string_view> reference = json.asString();
        if (!reference)
        {
            return unexpected(CommandError::invalid(
                "/value", "must be an Entity reference (a path or id) for an Entity key"));
        }
        Expected<Entity, CommandError> entity = context.entity(*reference, "/value");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        value.entity = *entity;
        return value;
    }
    }
    return unexpected(CommandError::invalid("/value", "does not fit the type of the key"));
}

Expected<AiSetValueResult, CommandError> aiSetValue(CommandContext& context, const AiSetValueParams& params)
{
    Expected<AiWorld*, CommandError> found = requireAi(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    AiWorld& ai = **found;
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    if (!isAgent(context.world(), *entity))
    {
        return unexpected(aiFailure(AiError::NoAgent, "/entity"));
    }
    Expected<JsonDocument, CommandError> json = parseParam(params.value, "/value");
    if (!json)
    {
        return unexpected(std::move(json).error());
    }
    ai.waitIdle();
    const Expected<BlackboardValue, AiError> current = ai.value(*entity, params.key);
    if (!current)
    {
        return unexpected(
            aiFailure(current.error(), current.error() == AiError::UnknownKey ? "/key" : "/entity"));
    }
    Expected<BlackboardValue, CommandError> value = parseValue(context, current->type, json->root());
    if (!value)
    {
        return unexpected(std::move(value).error());
    }
    if (!context.dryRun())
    {
        if (const Expected<void, AiError> written = ai.setValue(*entity, params.key, *value); !written)
        {
            return unexpected(aiFailure(written.error(), "/value"));
        }
    }
    return AiSetValueResult{.entity = context.summary(*entity), .key = keyOf(context, params.key, *value)};
}

bool finite(const Vec3& point) noexcept
{
    return std::abs(point.x) <= kMaxCoordinate && std::abs(point.y) <= kMaxCoordinate &&
           std::abs(point.z) <= kMaxCoordinate;
}

// Perceivers that would hear @p noise, as AiWorld tests it (without line of sight, which hearing ignores).
u32 listenersOf(World& world, const AiNoise& noise)
{
    u32 count = 0;
    world.query<const AiPerception, const Transform>().without<Inactive>().each(
        [&](Entity entity, const AiPerception& perception, const Transform& transform) {
            const Vec3 ear = transform.position + Vec3(0.0f, perception.eyeHeight, 0.0f);
            if (entity != noise.instigator && ((perception.detectTeams >> noise.team) & 1u) != 0 &&
                hearsNoise(ear, perception.hearing, noise))
            {
                ++count;
            }
        });
    return count;
}

Expected<AiNoiseResult, CommandError> aiNoise(CommandContext& context, const AiNoiseParams& params)
{
    Expected<AiWorld*, CommandError> found = requireAi(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    if (!finite(params.position))
    {
        return unexpected(
            CommandError::invalid("/position", "must be finite coordinates within 1,000,000 m"));
    }
    const f32 radius = params.radius.value_or(10.0f);
    if (!std::isfinite(radius) || radius < 0.0f || radius > kMaxCoordinate)
    {
        return unexpected(CommandError::invalid("/radius", "must be between 0 and 1,000,000 m"));
    }
    const u32 team = params.team.value_or(0);
    if (team >= kMaxAiTeams)
    {
        return unexpected(CommandError::invalid("/team", "must be between 0 and 31"));
    }
    AiNoise noise{.position = params.position, .radius = radius, .instigator = Entity{}, .team = team};
    if (params.instigator)
    {
        Expected<Entity, CommandError> instigator = context.entity(*params.instigator, "/instigator");
        if (!instigator)
        {
            return unexpected(std::move(instigator).error());
        }
        noise.instigator = *instigator;
    }
    if (!context.dryRun())
    {
        if (const Expected<void, AiError> reported = (*found)->reportNoise(noise); !reported)
        {
            return unexpected(aiFailure(reported.error(), "/radius"));
        }
    }
    return AiNoiseResult{.position = noise.position,
                         .radius = noise.radius,
                         .team = noise.team,
                         .listeners = listenersOf(context.world(), noise)};
}

Expected<AiValidateResult, CommandError> aiValidate(CommandContext& context, const AiValidateParams& params)
{
    std::filesystem::path real;
    if (!params.path.starts_with(kAssetsRoot) || lowerExtension(params.path) != kBehaviorTreeExtension ||
        params.path.size() <= kAssetsRoot.size() + kBehaviorTreeExtension.size() ||
        context.editor().files().resolve(params.path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path", "must be a .behavior file in the assets: mount, e.g. \"assets:/ai/guard.behavior\""));
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
    AiValidateResult result;
    result.path = params.path;
    const Expected<BehaviorTree, std::string> tree = parseBehaviorTree(text);
    if (tree)
    {
        result.valid = true;
        result.nodes = static_cast<u32>(tree->nodes.size());
        result.keys = static_cast<u32>(tree->blackboard.size());
        return result;
    }
    result.error = tree.error();
    const usize colon = result.error.find(": ");
    if (colon != std::string::npos && result.error.find(' ') > colon)
    {
        result.errorPath = result.error.substr(0, colon);
    }
    return result;
}

} // namespace

void registerAiCommands(CommandRegistry& registry)
{
    registry.add<&aiStatus>({
        .name = "ai.status",
        .title = strings::kCommandAiStatusTitle,
        .description = strings::kCommandAiStatusDescription,
        .agentSummary =
            "Reports what the AI of the running play session does: per agent (an Entity with a BehaviorAgent "
            "or an AiPerception; entity names one, otherwise up to 100) its tree file, whether the tree "
            "has loaded, the name of the deepest running node, the ticks, every blackboard key with its "
            "type and value, and what it perceives or remembers (sense, position, age, whether it is "
            "sensed now). Also counts the trees ticked by the last update, the agents waiting for their "
            "tree and the trees that failed. Needs a play session (play.start); wait false skips waiting "
            "for trees to load. Example: {\"entity\": \"/Creature\"}.",
        .category = "AI",
        .icon = "psychology",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&aiSetValue>({
        .name = "ai.setValue",
        .title = strings::kCommandAiSetValueTitle,
        .description = strings::kCommandAiSetValueDescription,
        .agentSummary =
            "Writes a key of the blackboard of an agent in the running play session; the tree sees it at the "
            "next AI step. The value must fit the key's type: true or false for Bool, a whole number for "
            "Int, a number for Float, [x, y, z] in metres for Vector3, an Entity reference for Entity; "
            "null clears a Vector3 or Entity key. Not undoable: the blackboard is runtime state. Use "
            "ai.status for the keys. Example: {\"entity\": \"/Creature\", \"key\": \"target\", \"value\": "
            "\"/Player\"}.",
        .category = "AI",
        .icon = "edit_note",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&aiNoise>({
        .name = "ai.noise",
        .title = strings::kCommandAiNoiseTitle,
        .description = strings::kCommandAiNoiseDescription,
        .agentSummary =
            "Makes a noise at a world position (metres) in the running play session: perceivers within "
            "radius times their hearing that notice its team hear it at the next AI step, as if a module "
            "had called AiService::reportNoise(). instigator is who makes it (a perceiver does not hear "
            "its own noise). The result counts the perceivers in reach. Not undoable. Example: "
            "{\"position\": [4, 0, 2], \"radius\": 12, \"instigator\": \"/Player\"}.",
        .category = "AI",
        .icon = "campaign",
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&aiValidate>({
        .name = "ai.validate",
        .title = strings::kCommandAiValidateTitle,
        .description = strings::kCommandAiValidateDescription,
        .agentSummary =
            "Checks a .behavior file of the project: valid is true when it parses (then nodes and keys count "
            "them). Otherwise error is the first problem and errorPath the JSON path of the offending value, "
            "e.g. \"root.selector[0].guard.key\". A file that does not parse is a result, not a failed "
            "command. Use it after writing a tree with asset tools. Example: {\"path\": "
            "\"assets:/ai/guard.behavior\"}.",
        .category = "AI",
        .icon = "fact_check",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
}

} // namespace illusion::editor::detail
