#include "CommandSupport.hpp"

namespace illusion::editor::detail
{
namespace
{

Expected<EntityResult, CommandError> hierarchySetParent(CommandContext& context,
                                                        const SetParentParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Entity parent;
    if (params.parent)
    {
        Expected<Entity, CommandError> resolved = context.entity(*params.parent, "/parent");
        if (!resolved)
        {
            return unexpected(std::move(resolved).error());
        }
        parent = *resolved;
    }
    const ParentMode mode = params.keepWorld.value_or(true) ? ParentMode::KeepWorld : ParentMode::KeepLocal;
    if (Expected<void, CommandError> moved =
            context.edit().setParent(*entity, parent, mode, params.index.value_or(kLastSibling));
        !moved)
    {
        return unexpected(std::move(moved).error());
    }
    return EntityResult{context.summary(*entity)};
}

Expected<EntityResult, CommandError> hierarchyReorder(CommandContext& context, const ReorderParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    if (Expected<void, CommandError> moved = context.edit().reorder(*entity, params.index); !moved)
    {
        return unexpected(std::move(moved).error());
    }
    return EntityResult{context.summary(*entity)};
}

Expected<EntitiesResult, CommandError> hierarchyChildren(CommandContext& context,
                                                         const ChildrenParams& params)
{
    Entity parent;
    if (params.entity)
    {
        Expected<Entity, CommandError> resolved = context.entity(*params.entity, "/entity");
        if (!resolved)
        {
            return unexpected(std::move(resolved).error());
        }
        parent = *resolved;
    }
    std::vector<Entity> entities;
    context.index().children(parent, entities);
    EntitiesResult result;
    for (const Entity child : entities)
    {
        result.entities.push_back(context.summary(child));
    }
    return result;
}

} // namespace

void registerHierarchyCommands(CommandRegistry& registry)
{
    registry.add<&hierarchySetParent>({
        .name = "hierarchy.set_parent",
        .title = strings::kCommandHierarchySetParentTitle,
        .description = strings::kCommandHierarchySetParentDescription,
        .agentSummary =
            "Moves an entity under a parent (absent or null: to the roots) at an index, keeping its "
            "world transform unless keepWorld is false. Example: {\"entity\": \"/Cell\", \"parent\": "
            "\"/Game/Board\", \"index\": 0}.",
        .category = "Hierarchy",
        .icon = "subdirectory_arrow_right",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&hierarchyReorder>({
        .name = "hierarchy.reorder",
        .title = strings::kCommandHierarchyReorderTitle,
        .description = strings::kCommandHierarchyReorderDescription,
        .agentSummary =
            "Moves an entity to an index among its siblings. Example: {\"entity\": \"/Game/Board\", "
            "\"index\": 0}.",
        .category = "Hierarchy",
        .icon = "swap_vert",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&hierarchyChildren>({
        .name = "hierarchy.children",
        .title = strings::kCommandHierarchyChildrenTitle,
        .description = strings::kCommandHierarchyChildrenDescription,
        .agentSummary = "Lists an entity's children in order, or the roots without an entity. Example: "
                        "{\"entity\": \"/Game\"}.",
        .category = "Hierarchy",
        .icon = "account_tree",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail
