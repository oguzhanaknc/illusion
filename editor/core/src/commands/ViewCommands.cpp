#include "CommandSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>

namespace illusion::editor::detail
{
namespace
{

ViewStateResult viewResult(CommandContext& context)
{
    ViewStateResult result;
    EntityIndex& index = context.index();
    const ViewState& view = context.editor().view();
    for (const EntityGuid guid : view.hiddenEntities())
    {
        if (const Entity entity = index.find(guid); !entity.isNull())
        {
            result.hidden.push_back(context.summary(entity));
        }
    }
    for (const EntityGuid guid : view.lockedEntities())
    {
        if (const Entity entity = index.find(guid); !entity.isNull())
        {
            result.locked.push_back(context.summary(entity));
        }
    }
    return result;
}

Expected<ViewStateResult, CommandError> viewGet(CommandContext& context, const NoParams&)
{
    return viewResult(context);
}

Expected<ViewStateResult, CommandError> viewSetEntities(CommandContext& context, const ViewSetParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    if (!params.hidden && !params.locked)
    {
        return unexpected(CommandError::invalid("", "must set hidden, locked or both"));
    }
    if (context.dryRun())
    {
        return viewResult(context);
    }
    EntityIndex& index = context.index();
    ViewState& view = context.editor().view();
    const auto apply = [&](Entity entity) {
        const EntityGuid guid = index.guidOf(entity);
        if (params.hidden)
        {
            view.setHidden(guid, *params.hidden);
        }
        if (params.locked)
        {
            view.setLocked(guid, *params.locked);
        }
    };
    const HierarchyIndex& hierarchy = index.hierarchy();
    for (const Entity entity : *entities)
    {
        if (params.recursive.value_or(true) && hierarchy.contains(entity))
        {
            for (const Entity member : hierarchy.subtree(entity))
            {
                apply(member);
            }
        }
        else
        {
            apply(entity);
        }
    }
    return viewResult(context);
}

} // namespace

void registerViewCommands(CommandRegistry& registry)
{
    registry.add<&viewGet>({
        .name = "view.get",
        .title = strings::kCommandViewGetTitle,
        .description = strings::kCommandViewGetDescription,
        .agentSummary =
            "Lists the entities the user hid in viewports or locked against picking (per-user view "
            "state, not saved in the scene). Example: {}.",
        .category = "View",
        .icon = "visibility",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&viewSetEntities>({
        .name = "view.set_entities",
        .title = strings::kCommandViewSetEntitiesTitle,
        .description = strings::kCommandViewSetEntitiesDescription,
        .agentSummary =
            "Hides or shows entities in viewports and locks or unlocks them against picking, with "
            "their descendants unless recursive is false. Not undoable and not saved in the "
            "scene. Example: {\"entities\": [\"/Game/Board\"], \"hidden\": true}.",
        .category = "View",
        .icon = "visibility_off",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail
