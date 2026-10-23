#include "CommandSupport.hpp"

#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>

#include <mutex>

namespace illusion::editor
{

void reflectCoreCommandTypes(StructRegistry& structs)
{
    static std::once_flag once;
    std::call_once(once, [&structs] {
        reflectStruct<EntitySummary>("editor.EntitySummary", 1, structs)
            .field("id", &EntitySummary::id)
            .field("name", &EntitySummary::name)
            .field("path", &EntitySummary::path);

        reflectStruct<ListCommandsParams>("editor.ListCommandsParams", 1, structs)
            .field("category", &ListCommandsParams::category)
            .field("query", &ListCommandsParams::query);
        reflectStruct<CommandSummary>("editor.CommandSummary", 1, structs)
            .field("name", &CommandSummary::name)
            .field("version", &CommandSummary::version)
            .field("title", &CommandSummary::title)
            .field("summary", &CommandSummary::summary)
            .field("category", &CommandSummary::category)
            .field("permission", &CommandSummary::permission)
            .field("flags", &CommandSummary::flags);
        reflectStruct<ListCommandsResult>("editor.ListCommandsResult", 1, structs)
            .field("commands", &ListCommandsResult::commands);
        reflectStruct<DescribeParams>("editor.DescribeParams", 1, structs)
            .field("command", &DescribeParams::command);
        reflectStruct<DescribeResult>("editor.DescribeResult", 1, structs)
            .field("command", &DescribeResult::command)
            .field("description", &DescribeResult::description)
            .field("icon", &DescribeResult::icon)
            .field("mode", &DescribeResult::mode)
            .field("shortcut", &DescribeResult::shortcut)
            .field("params", &DescribeResult::params)
            .field("result", &DescribeResult::result);

        reflectStruct<ProjectInfoResult>("editor.ProjectInfoResult", 1, structs)
            .field("name", &ProjectInfoResult::name)
            .field("version", &ProjectInfoResult::version)
            .field("engineVersion", &ProjectInfoResult::engineVersion)
            .field("root", &ProjectInfoResult::root)
            .field("startupScene", &ProjectInfoResult::startupScene)
            .field("modules", &ProjectInfoResult::modules)
            .field("mounts", &ProjectInfoResult::mounts);
        reflectStruct<FilePathParams>("editor.FilePathParams", 1, structs)
            .field("path", &FilePathParams::path);
        reflectStruct<FileListParams>("editor.FileListParams", 1, structs)
            .field("path", &FileListParams::path)
            .field("recursive", &FileListParams::recursive);
        reflectStruct<FileEntry>("editor.FileEntry", 1, structs)
            .field("path", &FileEntry::path)
            .field("isDirectory", &FileEntry::isDirectory)
            .field("size", &FileEntry::size);
        reflectStruct<FileListResult>("editor.FileListResult", 1, structs)
            .field("entries", &FileListResult::entries);
        reflectStruct<FileReadResult>("editor.FileReadResult", 1, structs)
            .field("path", &FileReadResult::path)
            .field("text", &FileReadResult::text);
        reflectStruct<FileStatResult>("editor.FileStatResult", 1, structs)
            .field("path", &FileStatResult::path)
            .field("exists", &FileStatResult::exists)
            .field("isDirectory", &FileStatResult::isDirectory)
            .field("size", &FileStatResult::size);
        reflectStruct<FileWriteParams>("editor.FileWriteParams", 1, structs)
            .field("path", &FileWriteParams::path)
            .field("text", &FileWriteParams::text);
        reflectStruct<FileWriteResult>("editor.FileWriteResult", 1, structs)
            .field("path", &FileWriteResult::path)
            .field("created", &FileWriteResult::created);
        reflectStruct<FileMoveParams>("editor.FileMoveParams", 1, structs)
            .field("from", &FileMoveParams::from)
            .field("to", &FileMoveParams::to);
        reflectStruct<FilePathResult>("editor.FilePathResult", 1, structs)
            .field("path", &FilePathResult::path);
        reflectStruct<RecoverParams>("editor.RecoverParams", 1, structs)
            .field("journal", &RecoverParams::journal);
        reflectStruct<RecoverResult>("editor.RecoverResult", 1, structs)
            .field("journal", &RecoverResult::journal)
            .field("scene", &RecoverResult::scene)
            .field("operations", &RecoverResult::operations);

        reflectStruct<SceneNewParams>("editor.SceneNewParams", 1, structs)
            .field("discardChanges", &SceneNewParams::discardChanges);
        reflectStruct<SceneOpenParams>("editor.SceneOpenParams", 1, structs)
            .field("path", &SceneOpenParams::path)
            .field("discardChanges", &SceneOpenParams::discardChanges);
        reflectStruct<SceneSaveAsParams>("editor.SceneSaveAsParams", 1, structs)
            .field("path", &SceneSaveAsParams::path)
            .field("overwrite", &SceneSaveAsParams::overwrite);
        reflectStruct<SceneInfoResult>("editor.SceneInfoResult", 1, structs)
            .field("path", &SceneInfoResult::path)
            .field("name", &SceneInfoResult::name)
            .field("revision", &SceneInfoResult::revision)
            .field("dirty", &SceneInfoResult::dirty)
            .field("entities", &SceneInfoResult::entities)
            .field("roots", &SceneInfoResult::roots);
        reflectStruct<SceneFileResult>("editor.SceneFileResult", 1, structs)
            .field("path", &SceneFileResult::path)
            .field("entities", &SceneFileResult::entities);
        reflectStruct<SceneTreeParams>("editor.SceneTreeParams", 1, structs)
            .field("root", &SceneTreeParams::root)
            .field("depth", &SceneTreeParams::depth);
        reflectStruct<TreeNode>("editor.TreeNode", 1, structs)
            .field("id", &TreeNode::id)
            .field("name", &TreeNode::name)
            .field("path", &TreeNode::path)
            .field("depth", &TreeNode::depth)
            .field("children", &TreeNode::children)
            .field("enabled", &TreeNode::enabled);
        reflectStruct<SceneTreeResult>("editor.SceneTreeResult", 1, structs)
            .field("nodes", &SceneTreeResult::nodes);
        reflectStruct<SceneQueryParams>("editor.SceneQueryParams", 1, structs)
            .field("has", &SceneQueryParams::has)
            .field("without", &SceneQueryParams::without)
            .field("name", &SceneQueryParams::name)
            .field("under", &SceneQueryParams::under)
            .field("limit", &SceneQueryParams::limit)
            .field("cursor", &SceneQueryParams::cursor);
        reflectStruct<SceneQueryResult>("editor.SceneQueryResult", 1, structs)
            .field("entities", &SceneQueryResult::entities)
            .field("total", &SceneQueryResult::total)
            .field("next", &SceneQueryResult::next);

        reflectStruct<EntityFindParams>("editor.EntityFindParams", 1, structs)
            .field("name", &EntityFindParams::name)
            .field("limit", &EntityFindParams::limit);
        reflectStruct<EntityCreateParams>("editor.EntityCreateParams", 1, structs)
            .field("name", &EntityCreateParams::name)
            .field("parent", &EntityCreateParams::parent)
            .field("index", &EntityCreateParams::index)
            .field("components", &EntityCreateParams::components);
        reflectStruct<EntityResult>("editor.EntityResult", 1, structs).field("entity", &EntityResult::entity);
        reflectStruct<EntitiesParams>("editor.EntitiesParams", 1, structs)
            .field("entities", &EntitiesParams::entities);
        reflectStruct<EntityDestroyResult>("editor.EntityDestroyResult", 1, structs)
            .field("destroyed", &EntityDestroyResult::destroyed);
        reflectStruct<EntitiesResult>("editor.EntitiesResult", 1, structs)
            .field("entities", &EntitiesResult::entities);
        reflectStruct<EntityRenameParams>("editor.EntityRenameParams", 1, structs)
            .field("entity", &EntityRenameParams::entity)
            .field("name", &EntityRenameParams::name);
        reflectStruct<EntitySetEnabledParams>("editor.EntitySetEnabledParams", 1, structs)
            .field("entity", &EntitySetEnabledParams::entity)
            .field("enabled", &EntitySetEnabledParams::enabled);
        reflectStruct<EntityGetParams>("editor.EntityGetParams", 1, structs)
            .field("entity", &EntityGetParams::entity)
            .field("components", &EntityGetParams::components);
        reflectStruct<EntityGetResult>("editor.EntityGetResult", 1, structs)
            .field("entity", &EntityGetResult::entity)
            .field("enabled", &EntityGetResult::enabled)
            .field("parent", &EntityGetResult::parent)
            .field("children", &EntityGetResult::children)
            .field("components", &EntityGetResult::components);

        reflectStruct<ComponentTypesParams>("editor.ComponentTypesParams", 1, structs)
            .field("category", &ComponentTypesParams::category);
        reflectStruct<ComponentTypeInfo>("editor.ComponentTypeInfo", 1, structs)
            .field("name", &ComponentTypeInfo::name)
            .field("displayName", &ComponentTypeInfo::displayName)
            .field("category", &ComponentTypeInfo::category)
            .field("icon", &ComponentTypeInfo::icon)
            .field("description", &ComponentTypeInfo::description)
            .field("tag", &ComponentTypeInfo::tag)
            .field("runtime", &ComponentTypeInfo::runtime);
        reflectStruct<ComponentTypesResult>("editor.ComponentTypesResult", 1, structs)
            .field("types", &ComponentTypesResult::types);
        reflectStruct<ComponentTypeParams>("editor.ComponentTypeParams", 1, structs)
            .field("type", &ComponentTypeParams::type);
        reflectStruct<ComponentSchemaResult>("editor.ComponentSchemaResult", 1, structs)
            .field("type", &ComponentSchemaResult::type)
            .field("version", &ComponentSchemaResult::version)
            .field("schema", &ComponentSchemaResult::schema);
        reflectStruct<ComponentAddParams>("editor.ComponentAddParams", 1, structs)
            .field("entity", &ComponentAddParams::entity)
            .field("type", &ComponentAddParams::type)
            .field("value", &ComponentAddParams::value);
        reflectStruct<ComponentAddResult>("editor.ComponentAddResult", 1, structs)
            .field("entity", &ComponentAddResult::entity)
            .field("added", &ComponentAddResult::added);
        reflectStruct<ComponentRefParams>("editor.ComponentRefParams", 1, structs)
            .field("entity", &ComponentRefParams::entity)
            .field("type", &ComponentRefParams::type);
        reflectStruct<ComponentGetParams>("editor.ComponentGetParams", 1, structs)
            .field("entity", &ComponentGetParams::entity)
            .field("type", &ComponentGetParams::type)
            .field("path", &ComponentGetParams::path);
        reflectStruct<ComponentGetResult>("editor.ComponentGetResult", 1, structs)
            .field("value", &ComponentGetResult::value);
        reflectStruct<ComponentSetParams>("editor.ComponentSetParams", 1, structs)
            .field("entity", &ComponentSetParams::entity)
            .field("type", &ComponentSetParams::type)
            .field("value", &ComponentSetParams::value);
        reflectStruct<ComponentSetFieldParams>("editor.ComponentSetFieldParams", 1, structs)
            .field("entity", &ComponentSetFieldParams::entity)
            .field("type", &ComponentSetFieldParams::type)
            .field("path", &ComponentSetFieldParams::path)
            .field("value", &ComponentSetFieldParams::value);
        reflectStruct<ComponentChangeResult>("editor.ComponentChangeResult", 1, structs)
            .field("entity", &ComponentChangeResult::entity)
            .field("changed", &ComponentChangeResult::changed);

        reflectStruct<SetParentParams>("editor.SetParentParams", 1, structs)
            .field("entity", &SetParentParams::entity)
            .field("parent", &SetParentParams::parent)
            .field("index", &SetParentParams::index)
            .field("keepWorld", &SetParentParams::keepWorld);
        reflectStruct<ReorderParams>("editor.ReorderParams", 1, structs)
            .field("entity", &ReorderParams::entity)
            .field("index", &ReorderParams::index);
        reflectStruct<ChildrenParams>("editor.ChildrenParams", 1, structs)
            .field("entity", &ChildrenParams::entity);

        reflectStruct<HistoryStepParams>("editor.HistoryStepParams", 1, structs)
            .field("steps", &HistoryStepParams::steps);
        reflectStruct<HistoryResult>("editor.HistoryResult", 1, structs)
            .field("steps", &HistoryResult::steps)
            .field("position", &HistoryResult::position)
            .field("revision", &HistoryResult::revision);
        reflectStruct<HistoryJumpParams>("editor.HistoryJumpParams", 1, structs)
            .field("position", &HistoryJumpParams::position);
        reflectStruct<HistoryEntry>("editor.HistoryEntry", 1, structs)
            .field("index", &HistoryEntry::index)
            .field("label", &HistoryEntry::label)
            .field("origin", &HistoryEntry::origin)
            .field("applied", &HistoryEntry::applied);
        reflectStruct<HistoryListResult>("editor.HistoryListResult", 1, structs)
            .field("steps", &HistoryListResult::steps)
            .field("position", &HistoryListResult::position)
            .field("bytes", &HistoryListResult::bytes)
            .field("budget", &HistoryListResult::budget);

        reflectStruct<SelectionResult>("editor.SelectionResult", 1, structs)
            .field("entities", &SelectionResult::entities);
    });
}

void registerCoreCommands(CommandRegistry& registry)
{
    reflectCoreCommandTypes(ComponentRegistry::instance().structs());
    detail::registerEditorCommands(registry);
    detail::registerProjectCommands(registry);
    detail::registerSceneCommands(registry);
    detail::registerEntityCommands(registry);
    detail::registerComponentCommands(registry);
    detail::registerHierarchyCommands(registry);
    detail::registerSelectionCommands(registry);
    detail::registerHistoryCommands(registry);
}

namespace detail
{

std::string localized(text::Localization& localization, text::TextKey key)
{
    std::string out;
    text::TextOutput output(out);
    if (!localization.message(key).format({}, localization.context(), output))
    {
        return std::string(key.english());
    }
    return out;
}

Expected<JsonDocument, CommandError> parseParam(const JsonText& value, std::string_view pointer)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(value.text);
    if (!parsed)
    {
        return unexpected(CommandError::invalid(std::string(pointer), "must be valid JSON"));
    }
    return std::move(parsed).value();
}

Expected<void, CommandError> requireProject(CommandContext& context)
{
    if (context.editor().project() == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    return {};
}

bool globMatch(std::string_view pattern, std::string_view text) noexcept
{
    usize p = 0;
    usize t = 0;
    usize star = std::string_view::npos;
    usize resume = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            resume = t;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;
            t = ++resume;
        }
        else
        {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
    {
        ++p;
    }
    return p == pattern.size();
}

std::string joinPath(std::string_view directory, std::string_view name)
{
    std::string out(directory);
    if (!out.ends_with('/'))
    {
        out += '/';
    }
    out += name;
    return out;
}

SceneInfoResult sceneInfo(EditorContext& editor)
{
    SceneInfoResult info;
    info.path = editor.document().path();
    info.name = std::string(editor.document().displayName());
    info.revision = editor.revision();
    info.dirty = editor.dirty();
    info.entities = editor.world().aliveCount();
    std::vector<Entity> roots;
    editor.entities().children(Entity{}, roots);
    info.roots = static_cast<u32>(roots.size());
    return info;
}

} // namespace detail
} // namespace illusion::editor
