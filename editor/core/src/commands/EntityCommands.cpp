#include "CommandSupport.hpp"

#include <illusion/ecs/Name.hpp>

#include <algorithm>

namespace illusion::editor::detail
{
namespace
{

Expected<EntityResult, CommandError> entityCreate(CommandContext& context, const EntityCreateParams& params)
{
    CreateEntityOptions options;
    if (params.name)
    {
        if (const std::string_view problem = entityNameError(*params.name);
            !params.name->empty() && !problem.empty())
        {
            return unexpected(CommandError::invalid("/name", std::string(problem)));
        }
        options.name = *params.name;
    }
    if (params.parent)
    {
        Expected<Entity, CommandError> parent = context.entity(*params.parent, "/parent");
        if (!parent)
        {
            return unexpected(std::move(parent).error());
        }
        options.parent = *parent;
    }
    options.index = params.index;
    std::optional<JsonDocument> components;
    if (params.components)
    {
        Expected<JsonDocument, CommandError> parsed = parseParam(*params.components, "/components");
        if (!parsed)
        {
            return unexpected(std::move(parsed).error());
        }
        components = std::move(parsed).value();
        options.components = components->root();
    }
    Expected<Entity, CommandError> created = context.edit().create(options);
    if (!created)
    {
        return unexpected(withPointerPrefix(std::move(created).error(), "/components"));
    }
    return EntityResult{context.summary(*created)};
}

Expected<EntityDestroyResult, CommandError> entityDestroy(CommandContext& context,
                                                          const EntitiesParams& params)
{
    std::vector<Entity> entities;
    if (Expected<void, CommandError> resolved = context.entities(params.entities, "/entities", entities);
        !resolved)
    {
        return unexpected(std::move(resolved).error());
    }
    if (entities.empty())
    {
        return unexpected(CommandError::invalid("/entities", "must name at least one entity"));
    }
    EntityDestroyResult result;
    for (const Entity entity : entities)
    {
        if (!context.world().isAlive(entity))
        {
            continue; // a descendant of one destroyed before
        }
        Expected<u32, CommandError> destroyed = context.edit().destroy(entity);
        if (!destroyed)
        {
            return unexpected(std::move(destroyed).error());
        }
        result.destroyed += *destroyed;
    }
    // Drop destroyed entities from the selection (undo restores it).
    Selection& selection = context.editor().selection();
    std::vector<EntityGuid> alive;
    for (const EntityGuid guid : selection.items())
    {
        if (!context.index().find(guid).isNull())
        {
            alive.push_back(guid);
        }
    }
    selection.set(alive);
    return result;
}

Expected<EntitiesResult, CommandError> entityDuplicate(CommandContext& context, const EntitiesParams& params)
{
    std::vector<Entity> entities;
    if (Expected<void, CommandError> resolved = context.entities(params.entities, "/entities", entities);
        !resolved)
    {
        return unexpected(std::move(resolved).error());
    }
    if (entities.empty())
    {
        return unexpected(CommandError::invalid("/entities", "must name at least one entity"));
    }
    EntitiesResult result;
    std::vector<EntityGuid> copies;
    for (const Entity entity : entities)
    {
        Expected<Entity, CommandError> copy = context.edit().duplicate(entity);
        if (!copy)
        {
            return unexpected(std::move(copy).error());
        }
        result.entities.push_back(context.summary(*copy));
        copies.push_back(context.index().guidOf(*copy));
    }
    context.editor().selection().set(copies);
    return result;
}

Expected<EntityResult, CommandError> entityRename(CommandContext& context, const EntityRenameParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    if (Expected<void, CommandError> renamed = context.edit().rename(*entity, params.name); !renamed)
    {
        return unexpected(withPointerPrefix(std::move(renamed).error(), "/name"));
    }
    return EntityResult{context.summary(*entity)};
}

Expected<EntityResult, CommandError> entitySetEnabled(CommandContext& context,
                                                      const EntitySetEnabledParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    if (Expected<void, CommandError> set = context.edit().setEnabled(*entity, params.enabled); !set)
    {
        return unexpected(std::move(set).error());
    }
    return EntityResult{context.summary(*entity)};
}

Expected<EntityGetResult, CommandError> entityGet(CommandContext& context, const EntityGetParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
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
    World& world = context.world();
    const ComponentRegistry& registry = world.registry();
    EntityGetResult result;
    result.entity = context.summary(*entity);
    result.enabled = !world.has<Disabled>(*entity);
    if (const Entity parent = parentOf(world, *entity); !parent.isNull())
    {
        result.parent = context.summary(parent).id;
    }
    std::vector<Entity> children;
    context.index().children(*entity, children);
    result.children = static_cast<u32>(children.size());
    std::string components = "{";
    for (const ComponentId type : world.componentsOf(*entity))
    {
        const TypeReflection* reflection = registry.reflection(type);
        if (reflection == nullptr || type == componentId<EntityGuid>() ||
            (!only.empty() && std::find(only.begin(), only.end(), type) == only.end()))
        {
            continue;
        }
        const std::optional<std::string> value = context.edit().capture(*entity, type);
        if (!value)
        {
            continue;
        }
        if (components.size() > 1)
        {
            components += ',';
        }
        components += '"';
        components += reflection->name; // stable names need no escaping
        components += "\":";
        components += *value;
    }
    components += '}';
    result.components.text = std::move(components);
    return result;
}

Expected<SceneQueryResult, CommandError> entityFind(CommandContext& context, const EntityFindParams& params)
{
    if (params.limit && (*params.limit == 0 || *params.limit > 10000))
    {
        return unexpected(CommandError::invalid("/limit", "must be between 1 and 10000"));
    }
    const u32 limit = params.limit.value_or(100);
    SceneQueryResult result;
    std::vector<Entity> matches;
    context.world().query<const Name>().each([&](Entity entity, const Name& name) {
        if (globMatch(params.name, name.value))
        {
            matches.push_back(entity);
        }
    });
    EntityIndex& index = context.index();
    std::sort(matches.begin(), matches.end(),
              [&index](Entity a, Entity b) { return index.guidOf(a).value < index.guidOf(b).value; });
    result.total = static_cast<u32>(matches.size());
    for (usize i = 0; i < matches.size() && i < limit; ++i)
    {
        result.entities.push_back(context.summary(matches[i]));
    }
    return result;
}

} // namespace

void registerEntityCommands(CommandRegistry& registry)
{
    registry.add<&entityCreate>({
        .name = "entity.create",
        .title = strings::kCommandEntityCreateTitle,
        .description = strings::kCommandEntityCreateDescription,
        .agentSummary =
            "Creates an entity with a new id, an optional name, parent and position, and Components "
            "given as {stable name: value}. Example: {\"name\": \"Board\", \"parent\": \"/Game\", "
            "\"components\": {\"illusion.Transform\": {\"position\": [4.5, 9.5, 0]}}}.",
        .category = "Entity",
        .icon = "add_circle",
        .flags = {.undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+Shift+N",
    });
    registry.add<&entityDestroy>({
        .name = "entity.destroy",
        .title = strings::kCommandEntityDestroyTitle,
        .description = strings::kCommandEntityDestroyDescription,
        .agentSummary = "Deletes entities with their descendants; entity fields elsewhere that point at them "
                        "become null (undo restores both). Example: {\"entities\": [\"$selection\"]}.",
        .category = "Entity",
        .icon = "delete",
        .flags = {.destructive = true, .undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Delete",
    });
    registry.add<&entityDuplicate>({
        .name = "entity.duplicate",
        .title = strings::kCommandEntityDuplicateTitle,
        .description = strings::kCommandEntityDuplicateDescription,
        .agentSummary =
            "Copies entities with their descendants and new ids next to the originals, and selects "
            "the copies. Example: {\"entities\": [\"/Game/Board/Cell 3\"]}.",
        .category = "Entity",
        .icon = "content_copy",
        .flags = {.undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+D",
    });
    registry.add<&entityRename>({
        .name = "entity.rename",
        .title = strings::kCommandEntityRenameTitle,
        .description = strings::kCommandEntityRenameDescription,
        .agentSummary = "Sets an entity's name (no '/', at most 256 bytes; \"\" removes it). Example: "
                        "{\"entity\": \"name:Board\", \"name\": \"Playfield\"}.",
        .category = "Entity",
        .icon = "edit",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "F2",
    });
    registry.add<&entitySetEnabled>({
        .name = "entity.set_enabled",
        .title = strings::kCommandEntitySetEnabledTitle,
        .description = strings::kCommandEntitySetEnabledDescription,
        .agentSummary =
            "Adds or removes the Disabled tag, which switches the entity and its descendants off. "
            "Example: {\"entity\": \"/Game/Ghost\", \"enabled\": false}.",
        .category = "Entity",
        .icon = "visibility",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&entityGet>({
        .name = "entity.get",
        .title = strings::kCommandEntityGetTitle,
        .description = strings::kCommandEntityGetDescription,
        .agentSummary = "Returns an entity's id, name, path, parent, child count and Component values "
                        "(optionally only some). Example: {\"entity\": \"/Game/Board\", \"components\": "
                        "[\"illusion.Transform\"]}.",
        .category = "Entity",
        .icon = "info",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&entityFind>({
        .name = "entity.find",
        .title = strings::kCommandEntityFindTitle,
        .description = strings::kCommandEntityFindDescription,
        .agentSummary = "Finds entities whose name matches a * and ? pattern, ordered by id. Example: "
                        "{\"name\": \"Cell *\", \"limit\": 20}.",
        .category = "Entity",
        .icon = "search",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail
