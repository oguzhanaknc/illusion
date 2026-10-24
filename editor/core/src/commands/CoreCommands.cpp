#include "CommandSupport.hpp"

#include <illusion/core/Hash.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/editor/core/Preferences.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>

#include <format>
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
        reflectStruct<BatchStep>("editor.BatchStep", 1, structs)
            .field("as", &BatchStep::as)
            .field("method", &BatchStep::method)
            .field("params", &BatchStep::params);
        reflectStruct<BatchParams>("editor.BatchParams", 1, structs)
            .field("label", &BatchParams::label)
            .field("atomic", &BatchParams::atomic)
            .field("steps", &BatchParams::steps);
        reflectStruct<BatchStepResult>("editor.BatchStepResult", 1, structs)
            .field("result", &BatchStepResult::result)
            .field("error", &BatchStepResult::error);
        reflectStruct<BatchResult>("editor.BatchResult", 1, structs)
            .field("steps", &BatchResult::steps)
            .field("failed", &BatchResult::failed);
        reflectStruct<WaitParams>("editor.WaitParams", 1, structs).field("frames", &WaitParams::frames);
        reflectStruct<WaitResult>("editor.WaitResult", 1, structs).field("frames", &WaitResult::frames);

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
        reflectStruct<ProjectCreateParams>("editor.ProjectCreateParams", 1, structs)
            .field("directory", &ProjectCreateParams::directory)
            .field("name", &ProjectCreateParams::name)
            .field("template", &ProjectCreateParams::templateName)
            .field("discardChanges", &ProjectCreateParams::discardChanges);
        reflectStruct<ProjectOpenParams>("editor.ProjectOpenParams", 1, structs)
            .field("directory", &ProjectOpenParams::directory)
            .field("discardChanges", &ProjectOpenParams::discardChanges);
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
            .field("roots", &SceneInfoResult::roots)
            .field("hash", &SceneInfoResult::hash);
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
        reflectStruct<ComponentResetParams>("editor.ComponentResetParams", 1, structs)
            .field("entity", &ComponentResetParams::entity)
            .field("type", &ComponentResetParams::type)
            .field("path", &ComponentResetParams::path);
        reflectStruct<ComponentCopyResult>("editor.ComponentCopyResult", 1, structs)
            .field("type", &ComponentCopyResult::type)
            .field("value", &ComponentCopyResult::value);
        reflectStruct<ComponentPasteResult>("editor.ComponentPasteResult", 1, structs)
            .field("type", &ComponentPasteResult::type)
            .field("entities", &ComponentPasteResult::entities)
            .field("added", &ComponentPasteResult::added);

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
        reflectStruct<HistoryUndoParams>("editor.HistoryUndoParams", 1, structs)
            .field("steps", &HistoryUndoParams::steps)
            .field("scope", &HistoryUndoParams::scope);
        reflectStruct<TransactionBeginParams>("editor.TransactionBeginParams", 1, structs)
            .field("label", &TransactionBeginParams::label);
        reflectStruct<TransactionBeginResult>("editor.TransactionBeginResult", 1, structs)
            .field("label", &TransactionBeginResult::label);
        reflectStruct<TransactionEndResult>("editor.TransactionEndResult", 1, structs)
            .field("commands", &TransactionEndResult::commands)
            .field("operations", &TransactionEndResult::operations)
            .field("position", &TransactionEndResult::position)
            .field("revision", &TransactionEndResult::revision);
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
        reflectStruct<ViewSetParams>("editor.ViewSetParams", 1, structs)
            .field("entities", &ViewSetParams::entities)
            .field("hidden", &ViewSetParams::hidden)
            .field("locked", &ViewSetParams::locked)
            .field("recursive", &ViewSetParams::recursive);
        reflectStruct<ViewStateResult>("editor.ViewStateResult", 1, structs)
            .field("hidden", &ViewStateResult::hidden)
            .field("locked", &ViewStateResult::locked);

        reflectStruct<SettingsGetParams>("editor.SettingsGetParams", 1, structs)
            .field("type", &SettingsGetParams::type);
        reflectStruct<SettingsEntry>("editor.SettingsEntry", 1, structs)
            .field("type", &SettingsEntry::type)
            .field("known", &SettingsEntry::known)
            .field("present", &SettingsEntry::present)
            .field("version", &SettingsEntry::version)
            .field("file", &SettingsEntry::file)
            .field("value", &SettingsEntry::value);
        reflectStruct<SettingsGetResult>("editor.SettingsGetResult", 1, structs)
            .field("settings", &SettingsGetResult::settings);
        reflectStruct<SettingsSetParams>("editor.SettingsSetParams", 1, structs)
            .field("type", &SettingsSetParams::type)
            .field("value", &SettingsSetParams::value)
            .field("replace", &SettingsSetParams::replace);
        reflectStruct<SettingsSetResult>("editor.SettingsSetResult", 1, structs)
            .field("type", &SettingsSetResult::type)
            .field("changed", &SettingsSetResult::changed)
            .field("present", &SettingsSetResult::present)
            .field("value", &SettingsSetResult::value);

        const FieldMeta degrees{.unit = Unit::Degrees};
        reflectStruct<TransformGetParams>("editor.TransformGetParams", 1, structs)
            .field("entity", &TransformGetParams::entity)
            .field("space", &TransformGetParams::space);
        reflectStruct<TransformGetResult>("editor.TransformGetResult", 1, structs)
            .field("entity", &TransformGetResult::entity)
            .field("position", &TransformGetResult::position)
            .field("rotation", &TransformGetResult::rotation)
            .field("euler", &TransformGetResult::euler, degrees)
            .field("scale", &TransformGetResult::scale);
        reflectStruct<TransformSetParams>("editor.TransformSetParams", 1, structs)
            .field("entities", &TransformSetParams::entities)
            .field("space", &TransformSetParams::space)
            .field("position", &TransformSetParams::position)
            .field("rotation", &TransformSetParams::rotation)
            .field("euler", &TransformSetParams::euler, degrees)
            .field("scale", &TransformSetParams::scale);
        reflectStruct<TransformTranslateParams>("editor.TransformTranslateParams", 1, structs)
            .field("entities", &TransformTranslateParams::entities)
            .field("offset", &TransformTranslateParams::offset)
            .field("space", &TransformTranslateParams::space);
        reflectStruct<TransformRotateParams>("editor.TransformRotateParams", 1, structs)
            .field("entities", &TransformRotateParams::entities)
            .field("rotation", &TransformRotateParams::rotation)
            .field("euler", &TransformRotateParams::euler, degrees)
            .field("space", &TransformRotateParams::space)
            .field("pivot", &TransformRotateParams::pivot)
            .field("point", &TransformRotateParams::point);
        reflectStruct<TransformScaleParams>("editor.TransformScaleParams", 1, structs)
            .field("entities", &TransformScaleParams::entities)
            .field("factor", &TransformScaleParams::factor)
            .field("pivot", &TransformScaleParams::pivot)
            .field("point", &TransformScaleParams::point);
        reflectStruct<TransformResetParams>("editor.TransformResetParams", 1, structs)
            .field("entities", &TransformResetParams::entities)
            .field("position", &TransformResetParams::position)
            .field("rotation", &TransformResetParams::rotation)
            .field("scale", &TransformResetParams::scale);

        reflectStruct<ClipboardResult>("editor.ClipboardResult", 1, structs)
            .field("entities", &ClipboardResult::entities)
            .field("text", &ClipboardResult::text);
        reflectStruct<ClipboardPasteParams>("editor.ClipboardPasteParams", 1, structs)
            .field("parent", &ClipboardPasteParams::parent)
            .field("index", &ClipboardPasteParams::index)
            .field("text", &ClipboardPasteParams::text);

        reflectStruct<AssetReference>("editor.AssetReference", 1, structs)
            .field("asset", &AssetReference::asset)
            .field("source", &AssetReference::source)
            .field("entity", &AssetReference::entity)
            .field("owner", &AssetReference::owner)
            .field("field", &AssetReference::field);
        reflectStruct<AssetListParams>("editor.AssetListParams", 1, structs)
            .field("path", &AssetListParams::path)
            .field("recursive", &AssetListParams::recursive)
            .field("kind", &AssetListParams::kind);
        reflectStruct<AssetEntry>("editor.AssetEntry", 1, structs)
            .field("path", &AssetEntry::path)
            .field("kind", &AssetEntry::kind)
            .field("size", &AssetEntry::size);
        reflectStruct<AssetListResult>("editor.AssetListResult", 1, structs)
            .field("assets", &AssetListResult::assets);
        reflectStruct<AssetInfoResult>("editor.AssetInfoResult", 1, structs)
            .field("path", &AssetInfoResult::path)
            .field("kind", &AssetInfoResult::kind)
            .field("size", &AssetInfoResult::size)
            .field("references", &AssetInfoResult::references)
            .field("dependencies", &AssetInfoResult::dependencies);
        reflectStruct<AssetMoveParams>("editor.AssetMoveParams", 1, structs)
            .field("from", &AssetMoveParams::from)
            .field("to", &AssetMoveParams::to);
        reflectStruct<AssetMoveResult>("editor.AssetMoveResult", 1, structs)
            .field("from", &AssetMoveResult::from)
            .field("to", &AssetMoveResult::to)
            .field("references", &AssetMoveResult::references)
            .field("updated", &AssetMoveResult::updated);
        reflectStruct<AssetDeleteParams>("editor.AssetDeleteParams", 1, structs)
            .field("path", &AssetDeleteParams::path)
            .field("force", &AssetDeleteParams::force);
        reflectStruct<AssetDeleteResult>("editor.AssetDeleteResult", 1, structs)
            .field("path", &AssetDeleteResult::path)
            .field("dangling", &AssetDeleteResult::dangling);
        reflectStruct<AssetReferencesResult>("editor.AssetReferencesResult", 1, structs)
            .field("references", &AssetReferencesResult::references);
        reflectStruct<AssetDependenciesResult>("editor.AssetDependenciesResult", 1, structs)
            .field("dependencies", &AssetDependenciesResult::dependencies);

        reflectStruct<MaterialCreateParams>("editor.MaterialCreateParams", 1, structs)
            .field("path", &MaterialCreateParams::path)
            .field("value", &MaterialCreateParams::value)
            .field("overwrite", &MaterialCreateParams::overwrite);
        reflectStruct<MaterialGetResult>("editor.MaterialGetResult", 1, structs)
            .field("path", &MaterialGetResult::path)
            .field("value", &MaterialGetResult::value);
        reflectStruct<MaterialSetParams>("editor.MaterialSetParams", 1, structs)
            .field("path", &MaterialSetParams::path)
            .field("value", &MaterialSetParams::value);
        reflectStruct<MaterialSetResult>("editor.MaterialSetResult", 1, structs)
            .field("path", &MaterialSetResult::path)
            .field("changed", &MaterialSetResult::changed);

        reflectStruct<LogReadParams>("editor.LogReadParams", 1, structs)
            .field("since", &LogReadParams::since)
            .field("minLevel", &LogReadParams::minLevel)
            .field("category", &LogReadParams::category)
            .field("contains", &LogReadParams::contains)
            .field("limit", &LogReadParams::limit);
        reflectStruct<LogEntry>("editor.LogEntry", 1, structs)
            .field("sequence", &LogEntry::sequence)
            .field("time", &LogEntry::time, {.unit = Unit::Seconds})
            .field("level", &LogEntry::level)
            .field("category", &LogEntry::category)
            .field("text", &LogEntry::text);
        reflectStruct<LogReadResult>("editor.LogReadResult", 1, structs)
            .field("entries", &LogReadResult::entries)
            .field("last", &LogReadResult::last)
            .field("dropped", &LogReadResult::dropped);
        reflectStruct<LogSetLevelParams>("editor.LogSetLevelParams", 1, structs)
            .field("level", &LogSetLevelParams::level);
        reflectStruct<LogStateResult>("editor.LogStateResult", 1, structs)
            .field("level", &LogStateResult::level)
            .field("last", &LogStateResult::last);

        reflectStruct<UiPanelEntry>("editor.UiPanelEntry", 1, structs)
            .field("id", &UiPanelEntry::id)
            .field("title", &UiPanelEntry::title)
            .field("open", &UiPanelEntry::open)
            .field("docked", &UiPanelEntry::docked);
        reflectStruct<UiLayoutEntry>("editor.UiLayoutEntry", 1, structs)
            .field("name", &UiLayoutEntry::name)
            .field("builtIn", &UiLayoutEntry::builtIn)
            .field("active", &UiLayoutEntry::active);
        reflectStruct<UiStateResult>("editor.UiStateResult", 1, structs)
            .field("panels", &UiStateResult::panels)
            .field("layouts", &UiStateResult::layouts);
        reflectStruct<UiShowPanelParams>("editor.UiShowPanelParams", 1, structs)
            .field("panel", &UiShowPanelParams::panel)
            .field("open", &UiShowPanelParams::open);
        reflectStruct<UiLayoutParams>("editor.UiLayoutParams", 1, structs)
            .field("name", &UiLayoutParams::name);
        reflectStruct<UiPaletteParams>("editor.UiPaletteParams", 1, structs)
            .field("mode", &UiPaletteParams::mode)
            .field("query", &UiPaletteParams::query);
        reflectStruct<UiNotifyParams>("editor.UiNotifyParams", 1, structs)
            .field("message", &UiNotifyParams::message)
            .field("level", &UiNotifyParams::level);
        reflectStruct<ScreenshotParams>("editor.ScreenshotParams", 1, structs)
            .field("width", &ScreenshotParams::width, {.min = 64.0, .max = 7680.0})
            .field("height", &ScreenshotParams::height, {.min = 64.0, .max = 4320.0});
        reflectStruct<ResultImage>("editor.ResultImage", 1, structs)
            .field("mimeType", &ResultImage::mimeType)
            .field("data", &ResultImage::data);
        reflectStruct<ScreenshotResult>("editor.ScreenshotResult", 1, structs)
            .field("width", &ScreenshotResult::width)
            .field("height", &ScreenshotResult::height)
            .field("_images", &ScreenshotResult::_images);
        reflectStruct<PreferencesSetParams>("editor.PreferencesSetParams", 1, structs)
            .field("value", &PreferencesSetParams::value)
            .field("replace", &PreferencesSetParams::replace);
        reflectStruct<PreferencesResult>("editor.PreferencesResult", 1, structs)
            .field("value", &PreferencesResult::value);
        reflectPreferences(structs);
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
    detail::registerTransformCommands(registry);
    detail::registerSelectionCommands(registry);
    detail::registerViewCommands(registry);
    detail::registerHistoryCommands(registry);
    detail::registerTransactionCommands(registry);
    detail::registerClipboardCommands(registry);
    detail::registerAssetCommands(registry);
    detail::registerMaterialCommands(registry);
    detail::registerLogCommands(registry);
    detail::registerSettingsCommands(registry);
    detail::registerUiCommands(registry);
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

Expected<std::vector<Entity>, CommandError> resolveEntityList(CommandContext& context,
                                                              const std::vector<std::string>& references)
{
    std::vector<Entity> entities;
    if (Expected<void, CommandError> resolved = context.entities(references, "/entities", entities);
        !resolved)
    {
        return unexpected(std::move(resolved).error());
    }
    if (entities.empty())
    {
        return unexpected(CommandError::invalid("/entities", "must name at least one entity"));
    }
    return entities;
}

Expected<std::string, CommandError> writeReflected(const TypeReflection& type, const void* value,
                                                   const EntityResolver* entities)
{
    JsonMutDocument doc;
    Expected<JsonMutValue, ValueError> written = writeValue(doc, type, value, {.entities = entities});
    if (!written)
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot write " + std::string(type.name) +
                                                                      ": " + written.error().message));
    }
    doc.setRoot(*written);
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot write " + std::string(type.name)));
    }
    return text;
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
    if (const Expected<std::string, CommandError> json = editor.sceneJson())
    {
        info.hash = std::format("{:016x}", xxhash64(std::as_bytes(std::span(json->data(), json->size()))));
    }
    return info;
}

} // namespace detail
} // namespace illusion::editor
