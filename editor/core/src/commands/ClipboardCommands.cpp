#include "CommandSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/SceneSerializer.hpp>

#include <unordered_set>

namespace illusion::editor::detail
{
namespace
{

struct Copied
{
    std::string text;
    u32 entities = 0;
};

// Scene v2 JSON of @p roots and their descendants. Each copied root keeps its world placement, so that
// pasting it anywhere puts it where it was.
Expected<Copied, CommandError> copyEntities(CommandContext& context, std::vector<Entity> roots)
{
    World& world = context.world();
    dropDescendants(world, roots);
    const HierarchyIndex& hierarchy = context.editor().entities().hierarchy();
    std::vector<Entity> subset;
    for (const Entity root : roots)
    {
        const std::span<const Entity> subtree = hierarchy.subtree(root);
        if (subtree.empty())
        {
            subset.push_back(root);
        }
        else
        {
            subset.insert(subset.end(), subtree.begin(), subtree.end());
        }
    }
    SceneSaveOptions options;
    options.subset = std::span<const Entity>(subset);
    options.order = SceneOrder::AsGiven;
    const SceneSaveResult saved = saveScene(world, options);
    if (!saved.ok())
    {
        return unexpected(
            CommandError::make(ErrorKind::Internal, "cannot copy the entities: " + saved.error));
    }

    // Write each root's world placement into a copy of the saved entities, then save that copy.
    World copy;
    const SceneLoadResult loaded = loadScene(copy, saved.json, {});
    if (!loaded.ok() || loaded.entities.size() != subset.size())
    {
        return unexpected(
            CommandError::make(ErrorKind::Internal, "cannot copy the entities: " + loaded.error));
    }
    std::unordered_set<u64> rootBits;
    for (const Entity root : roots)
    {
        rootBits.insert(root.toBits());
    }
    for (usize i = 0; i < subset.size(); ++i)
    {
        const Entity original = subset[i];
        if (!rootBits.contains(original.toBits()) ||
            (!world.has<Transform>(original) && !world.has<LocalTransform>(original)))
        {
            continue;
        }
        const Transform placed = worldTransformOf(world, original);
        const Entity entity = loaded.entities[i];
        if (Transform* transform = copy.tryGet<Transform>(entity))
        {
            *transform = placed;
        }
        else
        {
            copy.emplace<Transform>(entity, placed);
        }
        if (LocalTransform* local = copy.tryGet<LocalTransform>(entity))
        {
            *local = LocalTransform::from(placed);
        }
    }
    SceneSaveOptions copyOptions;
    copyOptions.subset = std::span<const Entity>(loaded.entities);
    copyOptions.order = SceneOrder::AsGiven;
    SceneSaveResult text = saveScene(copy, copyOptions);
    if (!text.ok())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot copy the entities: " + text.error));
    }
    return Copied{std::move(text.json), static_cast<u32>(subset.size())};
}

Expected<ClipboardResult, CommandError> clipboardCopy(CommandContext& context, const EntitiesParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    Expected<Copied, CommandError> copied = copyEntities(context, std::move(entities).value());
    if (!copied)
    {
        return unexpected(std::move(copied).error());
    }
    if (!context.dryRun())
    {
        context.editor().setClipboard(copied->text);
    }
    return ClipboardResult{copied->entities, std::move(copied->text)};
}

Expected<ClipboardResult, CommandError> clipboardCut(CommandContext& context, const EntitiesParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    dropDescendants(context.world(), *entities);
    Expected<Copied, CommandError> copied = copyEntities(context, *entities);
    if (!copied)
    {
        return unexpected(std::move(copied).error());
    }
    for (const Entity entity : *entities)
    {
        if (Expected<u32, CommandError> destroyed = context.edit().destroy(entity); !destroyed)
        {
            return unexpected(std::move(destroyed).error());
        }
    }
    // Cut entities leave the selection (undo restores it).
    Selection& selection = context.editor().selection();
    std::vector<EntityGuid> alive;
    for (const EntityGuid guid : selection.items())
    {
        if (!context.editor().entities().find(guid).isNull())
        {
            alive.push_back(guid);
        }
    }
    selection.set(alive);
    if (!context.dryRun())
    {
        context.editor().setClipboard(copied->text);
    }
    return ClipboardResult{copied->entities, std::move(copied->text)};
}

Expected<EntitiesResult, CommandError> clipboardPaste(CommandContext& context,
                                                      const ClipboardPasteParams& params)
{
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
    const std::string& text = params.text ? *params.text : context.editor().clipboard();
    if (text.empty())
    {
        return unexpected(params.text
                              ? CommandError::invalid("/text", "must be scene JSON")
                              : CommandError::make(ErrorKind::PreconditionFailed, "the clipboard is empty"));
    }
    Expected<std::vector<Entity>, CommandError> pasted = context.edit().paste(text, parent, params.index);
    if (!pasted)
    {
        return unexpected(withPointerPrefix(std::move(pasted).error(), "/text"));
    }
    if (pasted->empty())
    {
        return unexpected(CommandError::invalid("/text", "must hold at least one entity"));
    }
    EntitiesResult result;
    std::vector<EntityGuid> selected;
    for (const Entity entity : *pasted)
    {
        result.entities.push_back(context.summary(entity));
        selected.push_back(context.editor().entities().guidOf(entity));
    }
    context.editor().selection().set(selected);
    return result;
}

} // namespace

void registerClipboardCommands(CommandRegistry& registry)
{
    registry.add<&clipboardCopy>({
        .name = "clipboard.copy",
        .title = strings::kCommandClipboardCopyTitle,
        .description = strings::kCommandClipboardCopyDescription,
        .agentSummary =
            "Copies entities with their descendants to the editor clipboard as scene JSON (returned "
            "too); roots keep their world placement. Example: {\"entities\": [\"$selection\"]}.",
        .category = "Clipboard",
        .icon = "content_copy",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .palette = true,
        .defaultShortcut = "Ctrl+C",
    });
    registry.add<&clipboardCut>({
        .name = "clipboard.cut",
        .title = strings::kCommandClipboardCutTitle,
        .description = strings::kCommandClipboardCutDescription,
        .agentSummary = "Copies entities like clipboard.copy, then destroys them (undo brings them back). "
                        "Example: {\"entities\": [\"/Game/Board\"]}.",
        .category = "Clipboard",
        .icon = "content_cut",
        .flags = {.destructive = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+X",
    });
    registry.add<&clipboardPaste>({
        .name = "clipboard.paste",
        .title = strings::kCommandClipboardPasteTitle,
        .description = strings::kCommandClipboardPasteDescription,
        .agentSummary =
            "Pastes the clipboard (or the given scene JSON) as new entities with new ids, under a parent if "
            "given, keeping their world placement, and selects them. Example: {\"parent\": \"/Game\"}.",
        .category = "Clipboard",
        .icon = "content_paste",
        .flags = {.undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+V",
    });
}

} // namespace illusion::editor::detail
