#include "CommandSupport.hpp"

namespace illusion::editor::detail
{
namespace
{

SelectionResult selectionResult(CommandContext& context)
{
    SelectionResult result;
    EntityIndex& index = context.editor().entities();
    for (const EntityGuid guid : context.editor().selection().items())
    {
        if (const Entity entity = index.find(guid); !entity.isNull())
        {
            result.entities.push_back(context.summary(entity));
        }
    }
    return result;
}

Expected<std::vector<EntityGuid>, CommandError> resolveGuids(CommandContext& context,
                                                             const EntitiesParams& params)
{
    std::vector<Entity> entities;
    if (Expected<void, CommandError> resolved = context.entities(params.entities, "/entities", entities);
        !resolved)
    {
        return unexpected(std::move(resolved).error());
    }
    std::vector<EntityGuid> guids;
    for (const Entity entity : entities)
    {
        guids.push_back(context.editor().entities().guidOf(entity));
    }
    return guids;
}

Expected<SelectionResult, CommandError> selectionGet(CommandContext& context, const NoParams&)
{
    return selectionResult(context);
}

Expected<SelectionResult, CommandError> selectionSet(CommandContext& context, const EntitiesParams& params)
{
    Expected<std::vector<EntityGuid>, CommandError> guids = resolveGuids(context, params);
    if (!guids)
    {
        return unexpected(std::move(guids).error());
    }
    if (!context.dryRun())
    {
        context.editor().selection().set(*guids);
    }
    return selectionResult(context);
}

Expected<SelectionResult, CommandError> selectionAdd(CommandContext& context, const EntitiesParams& params)
{
    Expected<std::vector<EntityGuid>, CommandError> guids = resolveGuids(context, params);
    if (!guids)
    {
        return unexpected(std::move(guids).error());
    }
    if (!context.dryRun())
    {
        for (const EntityGuid guid : *guids)
        {
            context.editor().selection().add(guid);
        }
    }
    return selectionResult(context);
}

Expected<SelectionResult, CommandError> selectionRemove(CommandContext& context, const EntitiesParams& params)
{
    Expected<std::vector<EntityGuid>, CommandError> guids = resolveGuids(context, params);
    if (!guids)
    {
        return unexpected(std::move(guids).error());
    }
    if (!context.dryRun())
    {
        for (const EntityGuid guid : *guids)
        {
            context.editor().selection().remove(guid);
        }
    }
    return selectionResult(context);
}

Expected<SelectionResult, CommandError> selectionClear(CommandContext& context, const NoParams&)
{
    if (!context.dryRun())
    {
        context.editor().selection().clear();
    }
    return selectionResult(context);
}

} // namespace

void registerSelectionCommands(CommandRegistry& registry)
{
    registry.add<&selectionGet>({
        .name = "selection.get",
        .title = strings::kCommandSelectionGetTitle,
        .description = strings::kCommandSelectionGetDescription,
        .agentSummary = "Returns the selected entities, the primary (most recent) one last. Example: {}.",
        .category = "Selection",
        .icon = "select_all",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&selectionSet>({
        .name = "selection.set",
        .title = strings::kCommandSelectionSetTitle,
        .description = strings::kCommandSelectionSetDescription,
        .agentSummary = "Replaces the selection (not an undo step; undo restores selections with history). "
                        "Example: {\"entities\": [\"/Game/Board\", \"name:Player\"]}.",
        .category = "Selection",
        .icon = "check_box",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&selectionAdd>({
        .name = "selection.add",
        .title = strings::kCommandSelectionAddTitle,
        .description = strings::kCommandSelectionAddDescription,
        .agentSummary = "Adds entities to the selection; the last one becomes the primary. Example: "
                        "{\"entities\": [\"9f2c41aa0b7d3e15\"]}.",
        .category = "Selection",
        .icon = "add_box",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&selectionRemove>({
        .name = "selection.remove",
        .title = strings::kCommandSelectionRemoveTitle,
        .description = strings::kCommandSelectionRemoveDescription,
        .agentSummary = "Removes entities from the selection. Example: {\"entities\": [\"/Game/Board\"]}.",
        .category = "Selection",
        .icon = "indeterminate_check_box",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&selectionClear>({
        .name = "selection.clear",
        .title = strings::kCommandSelectionClearTitle,
        .description = strings::kCommandSelectionClearDescription,
        .agentSummary = "Deselects everything. Example: {}.",
        .category = "Selection",
        .icon = "deselect",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
}

} // namespace illusion::editor::detail
