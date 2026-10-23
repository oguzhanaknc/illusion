#include "CommandSupport.hpp"

#include <illusion/ecs/Name.hpp>

#include <charconv>

namespace illusion::editor::detail
{
namespace
{

constexpr u32 kDefaultLimit = 100;
constexpr u32 kMaxLimit = 10000;

Expected<void, CommandError> checkDiscard(CommandContext& context, const std::optional<bool>& discard)
{
    if (context.editor().dirty() && !discard.value_or(false))
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed, "the scene has unsaved changes; save it or pass discardChanges"));
    }
    return {};
}

Expected<SceneInfoResult, CommandError> sceneNew(CommandContext& context, const SceneNewParams& params)
{
    if (Expected<void, CommandError> allowed = checkDiscard(context, params.discardChanges); !allowed)
    {
        return unexpected(std::move(allowed).error());
    }
    if (!context.dryRun())
    {
        context.editor().newScene();
    }
    return sceneInfo(context.editor());
}

Expected<SceneInfoResult, CommandError> sceneOpen(CommandContext& context, const SceneOpenParams& params)
{
    if (Expected<void, CommandError> allowed = checkDiscard(context, params.discardChanges); !allowed)
    {
        return unexpected(std::move(allowed).error());
    }
    if (context.dryRun())
    {
        FileStat stat;
        const FileError error = context.editor().files().stat(params.path, stat);
        if (error == FileError::InvalidPath || error == FileError::UnknownMount)
        {
            return unexpected(CommandError::invalid("/path", "is not a valid virtual path"));
        }
        if (error != FileError::None || stat.isDirectory)
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "'" + params.path + "' is not a scene file"));
        }
        return sceneInfo(context.editor());
    }
    std::vector<std::string> warnings;
    if (Expected<void, CommandError> opened = context.editor().openScene(params.path, &warnings); !opened)
    {
        return unexpected(std::move(opened).error());
    }
    for (std::string& warning : warnings)
    {
        context.warn(std::move(warning));
    }
    return sceneInfo(context.editor());
}

Expected<SceneFileResult, CommandError> sceneSave(CommandContext& context, const NoParams&)
{
    const std::string path = context.editor().document().path();
    if (path.empty())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the scene has no file yet; use scene.save_as"));
    }
    if (context.dryRun())
    {
        return SceneFileResult{path, context.world().aliveCount()};
    }
    Expected<u32, CommandError> saved = context.editor().saveScene();
    if (!saved)
    {
        return unexpected(std::move(saved).error());
    }
    return SceneFileResult{path, *saved};
}

Expected<SceneFileResult, CommandError> sceneSaveAs(CommandContext& context, const SceneSaveAsParams& params)
{
    FileStat stat;
    const FileError error = context.editor().files().stat(params.path, stat);
    if (error == FileError::InvalidPath || error == FileError::UnknownMount)
    {
        return unexpected(CommandError::invalid("/path", "is not a valid virtual path"));
    }
    if (error == FileError::None && (stat.isDirectory || !params.overwrite.value_or(false)))
    {
        return unexpected(CommandError::make(ErrorKind::AlreadyExists,
                                             "'" + params.path + "' exists; pass overwrite to replace it"));
    }
    if (context.dryRun())
    {
        return SceneFileResult{params.path, context.world().aliveCount()};
    }
    Expected<u32, CommandError> saved = context.editor().saveScene(params.path);
    if (!saved)
    {
        return unexpected(std::move(saved).error());
    }
    return SceneFileResult{params.path, *saved};
}

Expected<SceneInfoResult, CommandError> sceneInfoCommand(CommandContext& context, const NoParams&)
{
    return sceneInfo(context.editor());
}

TreeNode treeNode(CommandContext& context, Entity entity, u32 depth)
{
    const EntitySummary summary = context.summary(entity);
    std::vector<Entity> children;
    context.editor().entities().children(entity, children);
    return TreeNode{summary.id,
                    summary.name,
                    summary.path,
                    depth,
                    static_cast<u32>(children.size()),
                    !context.world().has<Disabled>(entity)};
}

Expected<SceneTreeResult, CommandError> sceneTree(CommandContext& context, const SceneTreeParams& params)
{
    EntityIndex& index = context.editor().entities();
    std::vector<std::pair<Entity, u32>> stack; // entity, depth
    std::vector<Entity> level;
    if (params.root)
    {
        Expected<Entity, CommandError> root = context.entity(*params.root, "/root");
        if (!root)
        {
            return unexpected(std::move(root).error());
        }
        stack.emplace_back(*root, 0u);
    }
    else
    {
        index.children(Entity{}, level);
        for (auto it = level.rbegin(); it != level.rend(); ++it)
        {
            stack.emplace_back(*it, 0u);
        }
    }
    SceneTreeResult result;
    while (!stack.empty())
    {
        const auto [entity, depth] = stack.back();
        stack.pop_back();
        result.nodes.push_back(treeNode(context, entity, depth));
        if (params.depth && depth >= *params.depth)
        {
            continue;
        }
        level.clear();
        index.children(entity, level);
        for (auto it = level.rbegin(); it != level.rend(); ++it)
        {
            stack.emplace_back(*it, depth + 1);
        }
    }
    return result;
}

Expected<std::vector<ComponentId>, CommandError>
componentList(CommandContext& context, const std::optional<std::vector<std::string>>& names,
              std::string_view pointer)
{
    std::vector<ComponentId> types;
    if (!names)
    {
        return types;
    }
    for (usize i = 0; i < names->size(); ++i)
    {
        Expected<ComponentId, CommandError> type =
            context.componentType((*names)[i], std::string(pointer) + "/" + std::to_string(i));
        if (!type)
        {
            return unexpected(std::move(type).error());
        }
        types.push_back(*type);
    }
    return types;
}

Expected<u32, CommandError> readLimit(const std::optional<u32>& limit)
{
    if (limit && (*limit == 0 || *limit > kMaxLimit))
    {
        return unexpected(
            CommandError::invalid("/limit", "must be between 1 and " + std::to_string(kMaxLimit)));
    }
    return limit.value_or(kDefaultLimit);
}

// Every entity in hierarchy order (or the descendants of @p under), passed to fn until it returns false.
template <class F>
void forEachInOrder(CommandContext& context, Entity under, F&& fn)
{
    EntityIndex& index = context.editor().entities();
    const HierarchyIndex& hierarchy = index.hierarchy();
    if (!under.isNull())
    {
        const std::span<const Entity> subtree = hierarchy.subtree(under);
        for (usize i = 1; i < subtree.size(); ++i)
        {
            fn(subtree[i]);
        }
        return;
    }
    std::vector<Entity> roots;
    index.children(Entity{}, roots);
    for (const Entity root : roots)
    {
        const std::span<const Entity> subtree = hierarchy.subtree(root);
        if (subtree.empty())
        {
            fn(root);
            continue;
        }
        for (const Entity entity : subtree)
        {
            fn(entity);
        }
    }
}

Expected<SceneQueryResult, CommandError> sceneQuery(CommandContext& context, const SceneQueryParams& params)
{
    Expected<std::vector<ComponentId>, CommandError> has = componentList(context, params.has, "/has");
    if (!has)
    {
        return unexpected(std::move(has).error());
    }
    Expected<std::vector<ComponentId>, CommandError> without =
        componentList(context, params.without, "/without");
    if (!without)
    {
        return unexpected(std::move(without).error());
    }
    Expected<u32, CommandError> limit = readLimit(params.limit);
    if (!limit)
    {
        return unexpected(std::move(limit).error());
    }
    u32 offset = 0;
    if (params.cursor)
    {
        const std::string& text = *params.cursor;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), offset);
        if (error != std::errc{} || end != text.data() + text.size())
        {
            return unexpected(
                CommandError::invalid("/cursor", "must be the 'next' value of a previous query"));
        }
    }
    Entity under;
    if (params.under)
    {
        Expected<Entity, CommandError> entity = context.entity(*params.under, "/under");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        under = *entity;
    }
    World& world = context.world();
    std::vector<Entity> page;
    u32 total = 0;
    forEachInOrder(context, under, [&](Entity entity) {
        const std::span<const ComponentId> components = world.componentsOf(entity);
        const auto contains = [&](ComponentId type) {
            return std::find(components.begin(), components.end(), type) != components.end();
        };
        if (!std::all_of(has->begin(), has->end(), contains) ||
            std::any_of(without->begin(), without->end(), contains))
        {
            return;
        }
        if (params.name)
        {
            const Name* name = world.tryGet<Name>(entity);
            if (!globMatch(*params.name,
                           name != nullptr ? std::string_view(name->value) : std::string_view()))
            {
                return;
            }
        }
        if (total >= offset && page.size() < *limit)
        {
            page.push_back(entity);
        }
        ++total;
    });
    SceneQueryResult result;
    result.total = total;
    for (const Entity entity : page)
    {
        result.entities.push_back(context.summary(entity));
    }
    if (offset + page.size() < total)
    {
        result.next = std::to_string(offset + page.size());
    }
    return result;
}

} // namespace

void registerSceneCommands(CommandRegistry& registry)
{
    registry.add<&sceneNew>({
        .name = "scene.new",
        .title = strings::kCommandSceneNewTitle,
        .description = strings::kCommandSceneNewDescription,
        .agentSummary = "Replaces the open scene with an empty, untitled one; refuses when there are unsaved "
                        "changes unless discardChanges is true. Clears the undo history. Example: {}.",
        .category = "Scene",
        .icon = "add_box",
        .flags = {.destructive = true},
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+N",
    });
    registry.add<&sceneOpen>({
        .name = "scene.open",
        .title = strings::kCommandSceneOpenTitle,
        .description = strings::kCommandSceneOpenDescription,
        .agentSummary =
            "Opens a scene file in place of the open scene (unsaved changes need discardChanges). "
            "Example: {\"path\": \"assets:/scenes/main.scene\"}.",
        .category = "Scene",
        .icon = "folder_open",
        .flags = {.destructive = true},
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+O",
    });
    registry.add<&sceneSave>({
        .name = "scene.save",
        .title = strings::kCommandSceneSaveTitle,
        .description = strings::kCommandSceneSaveDescription,
        .agentSummary = "Saves the scene to its file (scene format v2); untitled scenes need scene.save_as. "
                        "Example: {}.",
        .category = "Scene",
        .icon = "save",
        .flags = {.idempotent = true},
        .permission = PermissionClass::Files,
        .palette = true,
        .defaultShortcut = "Ctrl+S",
    });
    registry.add<&sceneSaveAs>({
        .name = "scene.save_as",
        .title = strings::kCommandSceneSaveAsTitle,
        .description = strings::kCommandSceneSaveAsDescription,
        .agentSummary = "Saves the scene to a new file, which becomes the scene's file. Example: {\"path\": "
                        "\"assets:/scenes/level1.scene\", \"overwrite\": false}.",
        .category = "Scene",
        .icon = "save_as",
        .flags = {},
        .permission = PermissionClass::Files,
        .palette = true,
        .defaultShortcut = "Ctrl+Shift+S",
    });
    registry.add<&sceneInfoCommand>({
        .name = "scene.info",
        .title = strings::kCommandSceneInfoTitle,
        .description = strings::kCommandSceneInfoDescription,
        .agentSummary = "Returns the scene's file, revision, unsaved state and entity counts. Example: {}.",
        .category = "Scene",
        .icon = "info",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&sceneTree>({
        .name = "scene.tree",
        .title = strings::kCommandSceneTreeTitle,
        .description = strings::kCommandSceneTreeDescription,
        .agentSummary = "Lists entities in pre-order with depth and child counts, for the whole scene or one "
                        "subtree. Example: {\"root\": \"/Game\", \"depth\": 1}.",
        .category = "Scene",
        .icon = "account_tree",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&sceneQuery>({
        .name = "scene.query",
        .title = strings::kCommandSceneQueryTitle,
        .description = strings::kCommandSceneQueryDescription,
        .agentSummary = "Finds entities that have all of 'has', none of 'without', a name matching a * and ? "
                        "pattern and lie under an entity; pages with limit and cursor. Example: {\"has\": "
                        "[\"illusion.Transform\"], \"name\": \"Cell*\", \"limit\": 50}.",
        .category = "Scene",
        .icon = "search",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail
