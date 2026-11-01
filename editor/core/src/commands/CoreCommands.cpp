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

        const FieldMeta angle{.unit = Unit::Degrees};
        const FieldMeta pitch{.unit = Unit::Degrees, .min = -90.0, .max = 90.0};
        const FieldMeta fov{.unit = Unit::Degrees, .min = 1.0, .max = 170.0};
        const FieldMeta extent{.min = 0.01, .max = 100000.0};
        reflectStruct<ViewportCamera>("editor.ViewportCamera", 1, structs)
            .field("pivot", &ViewportCamera::pivot)
            .field("distance", &ViewportCamera::distance, extent)
            .field("yaw", &ViewportCamera::yaw, angle)
            .field("pitch", &ViewportCamera::pitch, pitch)
            .field("fieldOfView", &ViewportCamera::fieldOfView, fov)
            .field("projection", &ViewportCamera::projection)
            .field("orthographicHeight", &ViewportCamera::orthographicHeight, extent);
        reflectStruct<ViewportVisualisers>("editor.ViewportVisualisers", 1, structs)
            .field("lights", &ViewportVisualisers::lights)
            .field("cameras", &ViewportVisualisers::cameras)
            .field("colliders", &ViewportVisualisers::colliders)
            .field("characters", &ViewportVisualisers::characters)
            .field("joints", &ViewportVisualisers::joints)
            .field("audio", &ViewportVisualisers::audio);
        reflectStruct<ViewportSettings>("editor.ViewportSettings", 1, structs)
            .field("shading", &ViewportSettings::shading)
            .field("wireframe", &ViewportSettings::wireframe)
            .field("grid", &ViewportSettings::grid)
            .field("selectionOutline", &ViewportSettings::selectionOutline)
            .field("hover", &ViewportSettings::hover)
            .field("icons", &ViewportSettings::icons)
            .field("viewCube", &ViewportSettings::viewCube)
            .field("stats", &ViewportSettings::stats)
            .field("cameraPreview", &ViewportSettings::cameraPreview)
            .field("visualisers", &ViewportSettings::visualisers)
            .field("visualiserScope", &ViewportSettings::visualiserScope)
            .field("aspect", &ViewportSettings::aspect)
            .field("width", &ViewportSettings::width)
            .field("height", &ViewportSettings::height);
        reflectStruct<ViewportParams>("editor.ViewportParams", 1, structs)
            .field("viewport", &ViewportParams::viewport);
        reflectStruct<ViewportEntry>("editor.ViewportEntry", 1, structs)
            .field("id", &ViewportEntry::id)
            .field("kind", &ViewportEntry::kind)
            .field("width", &ViewportEntry::width)
            .field("height", &ViewportEntry::height)
            .field("visible", &ViewportEntry::visible)
            .field("camera", &ViewportEntry::camera)
            .field("eye", &ViewportEntry::eye)
            .field("settings", &ViewportEntry::settings)
            .field("sceneCamera", &ViewportEntry::sceneCamera);
        reflectStruct<ViewportListResult>("editor.ViewportListResult", 1, structs)
            .field("viewports", &ViewportListResult::viewports);
        reflectStruct<ViewportCameraResult>("editor.ViewportCameraResult", 1, structs)
            .field("viewport", &ViewportCameraResult::viewport)
            .field("camera", &ViewportCameraResult::camera)
            .field("eye", &ViewportCameraResult::eye)
            .field("forward", &ViewportCameraResult::forward);
        reflectStruct<ViewportCameraSetParams>("editor.ViewportCameraSetParams", 1, structs)
            .field("viewport", &ViewportCameraSetParams::viewport)
            .field("pivot", &ViewportCameraSetParams::pivot)
            .field("distance", &ViewportCameraSetParams::distance, extent)
            .field("yaw", &ViewportCameraSetParams::yaw, angle)
            .field("pitch", &ViewportCameraSetParams::pitch, pitch)
            .field("fieldOfView", &ViewportCameraSetParams::fieldOfView, fov)
            .field("projection", &ViewportCameraSetParams::projection)
            .field("orthographicHeight", &ViewportCameraSetParams::orthographicHeight, extent)
            .field("eye", &ViewportCameraSetParams::eye)
            .field("animate", &ViewportCameraSetParams::animate);
        reflectStruct<ViewportViewParams>("editor.ViewportViewParams", 1, structs)
            .field("viewport", &ViewportViewParams::viewport)
            .field("view", &ViewportViewParams::view)
            .field("animate", &ViewportViewParams::animate);
        reflectStruct<ViewportFrameParams>("editor.ViewportFrameParams", 1, structs)
            .field("viewport", &ViewportFrameParams::viewport)
            .field("entities", &ViewportFrameParams::entities)
            .field("animate", &ViewportFrameParams::animate);
        reflectStruct<ViewportModeParams>("editor.ViewportModeParams", 1, structs)
            .field("viewport", &ViewportModeParams::viewport)
            .field("shading", &ViewportModeParams::shading)
            .field("wireframe", &ViewportModeParams::wireframe);
        reflectStruct<ViewportVisualisersParams>("editor.ViewportVisualisersParams", 1, structs)
            .field("lights", &ViewportVisualisersParams::lights)
            .field("cameras", &ViewportVisualisersParams::cameras)
            .field("colliders", &ViewportVisualisersParams::colliders)
            .field("characters", &ViewportVisualisersParams::characters)
            .field("joints", &ViewportVisualisersParams::joints)
            .field("audio", &ViewportVisualisersParams::audio);
        reflectStruct<ViewportOverlaysParams>("editor.ViewportOverlaysParams", 1, structs)
            .field("viewport", &ViewportOverlaysParams::viewport)
            .field("grid", &ViewportOverlaysParams::grid)
            .field("selectionOutline", &ViewportOverlaysParams::selectionOutline)
            .field("hover", &ViewportOverlaysParams::hover)
            .field("icons", &ViewportOverlaysParams::icons)
            .field("viewCube", &ViewportOverlaysParams::viewCube)
            .field("stats", &ViewportOverlaysParams::stats)
            .field("cameraPreview", &ViewportOverlaysParams::cameraPreview)
            .field("visualisers", &ViewportOverlaysParams::visualisers)
            .field("visualiserScope", &ViewportOverlaysParams::visualiserScope);
        reflectStruct<ViewportSettingsResult>("editor.ViewportSettingsResult", 1, structs)
            .field("viewport", &ViewportSettingsResult::viewport)
            .field("settings", &ViewportSettingsResult::settings);
        reflectStruct<ViewportProjectParams>("editor.ViewportProjectParams", 1, structs)
            .field("viewport", &ViewportProjectParams::viewport)
            .field("entity", &ViewportProjectParams::entity)
            .field("point", &ViewportProjectParams::point);
        reflectStruct<ViewportProjectResult>("editor.ViewportProjectResult", 1, structs)
            .field("x", &ViewportProjectResult::x)
            .field("y", &ViewportProjectResult::y)
            .field("depth", &ViewportProjectResult::depth)
            .field("inFront", &ViewportProjectResult::inFront)
            .field("inside", &ViewportProjectResult::inside);
        reflectStruct<ViewportPickParams>("editor.ViewportPickParams", 1, structs)
            .field("viewport", &ViewportPickParams::viewport)
            .field("x", &ViewportPickParams::x, {.min = 0.0, .max = 16384.0})
            .field("y", &ViewportPickParams::y, {.min = 0.0, .max = 16384.0})
            .field("cycle", &ViewportPickParams::cycle)
            .field("select", &ViewportPickParams::select);
        reflectStruct<ViewportPickResult>("editor.ViewportPickResult", 1, structs)
            .field("entity", &ViewportPickResult::entity)
            .field("index", &ViewportPickResult::index)
            .field("candidates", &ViewportPickResult::candidates);
        reflectStruct<ViewportIdResult>("editor.ViewportIdResult", 1, structs)
            .field("viewport", &ViewportIdResult::viewport);
        const FieldMeta resolutionWidth{.min = 16.0, .max = 7680.0};
        const FieldMeta resolutionHeight{.min = 16.0, .max = 4320.0};
        reflectStruct<ViewportAspectParams>("editor.ViewportAspectParams", 1, structs)
            .field("viewport", &ViewportAspectParams::viewport)
            .field("aspect", &ViewportAspectParams::aspect)
            .field("width", &ViewportAspectParams::width, resolutionWidth)
            .field("height", &ViewportAspectParams::height, resolutionHeight);
        const FieldMeta pixel{.min = 0.0, .max = 16384.0};
        reflectStruct<ViewportSnapParams>("editor.ViewportSnapParams", 1, structs)
            .field("viewport", &ViewportSnapParams::viewport)
            .field("x", &ViewportSnapParams::x, {.min = 0.0, .max = 16384.0})
            .field("y", &ViewportSnapParams::y, {.min = 0.0, .max = 16384.0})
            .field("to", &ViewportSnapParams::to)
            .field("exclude", &ViewportSnapParams::exclude)
            .field("tolerance", &ViewportSnapParams::tolerance, {.min = 1.0, .max = 64.0});
        reflectStruct<ViewportSnapResult>("editor.ViewportSnapResult", 1, structs)
            .field("hit", &ViewportSnapResult::hit)
            .field("kind", &ViewportSnapResult::kind)
            .field("position", &ViewportSnapResult::position)
            .field("normal", &ViewportSnapResult::normal)
            .field("rotation", &ViewportSnapResult::rotation)
            .field("entity", &ViewportSnapResult::entity);
        reflectStruct<ViewportSampleParams>("editor.ViewportSampleParams", 1, structs)
            .field("viewport", &ViewportSampleParams::viewport)
            .field("x", &ViewportSampleParams::x, pixel)
            .field("y", &ViewportSampleParams::y, pixel)
            .field("radius", &ViewportSampleParams::radius, {.min = 0.0, .max = 8.0});
        reflectStruct<ViewportSampleResult>("editor.ViewportSampleResult", 1, structs)
            .field("color", &ViewportSampleResult::color, {.hint = FieldHint::Color})
            .field("hue", &ViewportSampleResult::hue, angle)
            .field("saturation", &ViewportSampleResult::saturation)
            .field("value", &ViewportSampleResult::value)
            .field("pixels", &ViewportSampleResult::pixels);
        reflectStruct<ViewportPickRectParams>("editor.ViewportPickRectParams", 1, structs)
            .field("viewport", &ViewportPickRectParams::viewport)
            .field("x", &ViewportPickRectParams::x, pixel)
            .field("y", &ViewportPickRectParams::y, pixel)
            .field("width", &ViewportPickRectParams::width, {.min = 1.0, .max = 16384.0})
            .field("height", &ViewportPickRectParams::height, {.min = 1.0, .max = 16384.0})
            .field("select", &ViewportPickRectParams::select);
        reflectStruct<ViewportPickRectResult>("editor.ViewportPickRectResult", 1, structs)
            .field("entities", &ViewportPickRectResult::entities);
        reflectStruct<CameraBookmarkParams>("editor.CameraBookmarkParams", 1, structs)
            .field("viewport", &CameraBookmarkParams::viewport)
            .field("slot", &CameraBookmarkParams::slot, {.min = 1.0, .max = 9.0})
            .field("animate", &CameraBookmarkParams::animate);
        reflectStruct<ViewportRenderStats>("editor.ViewportRenderStats", 1, structs)
            .field("frames", &ViewportRenderStats::frames)
            .field("candidates", &ViewportRenderStats::candidates)
            .field("visible", &ViewportRenderStats::visible)
            .field("drawCalls", &ViewportRenderStats::drawCalls)
            .field("triangles", &ViewportRenderStats::triangles)
            .field("shadowCasters", &ViewportRenderStats::shadowCasters)
            .field("shadowDrawCalls", &ViewportRenderStats::shadowDrawCalls)
            .field("lightsDropped", &ViewportRenderStats::lightsDropped);
        reflectStruct<RenderStatsResult>("editor.RenderStatsResult", 1, structs)
            .field("viewport", &RenderStatsResult::viewport)
            .field("width", &RenderStatsResult::width)
            .field("height", &RenderStatsResult::height)
            .field("stats", &RenderStatsResult::stats);
        reflectStruct<ViewportGpuInfo>("editor.ViewportGpuInfo", 1, structs)
            .field("name", &ViewportGpuInfo::name)
            .field("driver", &ViewportGpuInfo::driver)
            .field("vendorId", &ViewportGpuInfo::vendorId)
            .field("deviceId", &ViewportGpuInfo::deviceId)
            .field("apiVersion", &ViewportGpuInfo::apiVersion)
            .field("discrete", &ViewportGpuInfo::discrete)
            .field("software", &ViewportGpuInfo::software)
            .field("bindless", &ViewportGpuInfo::bindless)
            .field("validation", &ViewportGpuInfo::validation)
            .field("validationErrors", &ViewportGpuInfo::validationErrors);
        reflectStruct<RenderGpuInfoResult>("editor.RenderGpuInfoResult", 1, structs)
            .field("available", &RenderGpuInfoResult::available)
            .field("gpu", &RenderGpuInfoResult::gpu);
        reflectStruct<RenderDiagnosticsResult>("editor.RenderDiagnosticsResult", 1, structs)
            .field("available", &RenderDiagnosticsResult::available)
            .field("validation", &RenderDiagnosticsResult::validation)
            .field("validationErrors", &RenderDiagnosticsResult::validationErrors);

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
        const FieldMeta grid{.min = 0.001, .max = 1000.0};
        reflectStruct<TransformSnapParams>("editor.TransformSnapParams", 1, structs)
            .field("entities", &TransformSnapParams::entities)
            .field("increment", &TransformSnapParams::increment, grid);
        reflectStruct<TransformAlignParams>("editor.TransformAlignParams", 1, structs)
            .field("entities", &TransformAlignParams::entities)
            .field("axis", &TransformAlignParams::axis)
            .field("edge", &TransformAlignParams::edge)
            .field("to", &TransformAlignParams::to);
        reflectStruct<TransformDistributeParams>("editor.TransformDistributeParams", 1, structs)
            .field("entities", &TransformDistributeParams::entities)
            .field("axis", &TransformDistributeParams::axis)
            .field("mode", &TransformDistributeParams::mode);
        reflectStruct<TransformDropParams>("editor.TransformDropParams", 1, structs)
            .field("entities", &TransformDropParams::entities)
            .field("onto", &TransformDropParams::onto);
        reflectStruct<TransformAlignViewParams>("editor.TransformAlignViewParams", 1, structs)
            .field("entities", &TransformAlignViewParams::entities)
            .field("viewport", &TransformAlignViewParams::viewport)
            .field("move", &TransformAlignViewParams::move);

        const FieldMeta snapAngle{.unit = Unit::Degrees, .min = 0.01, .max = 180.0};
        const FieldMeta snapScale{.min = 0.001, .max = 10.0};
        reflectStruct<GizmoSnap>("editor.GizmoSnap", 1, structs)
            .field("enabled", &GizmoSnap::enabled)
            .field("translate", &GizmoSnap::translate, grid)
            .field("rotate", &GizmoSnap::rotate, snapAngle)
            .field("scale", &GizmoSnap::scale, snapScale)
            .field("alignToNormal", &GizmoSnap::alignToNormal);
        reflectStruct<GizmoRuler>("editor.GizmoRuler", 1, structs)
            .field("from", &GizmoRuler::from)
            .field("to", &GizmoRuler::to);
        reflectStruct<GizmoSettings>("editor.GizmoSettings", 1, structs)
            .field("tool", &GizmoSettings::tool)
            .field("space", &GizmoSettings::space)
            .field("pivot", &GizmoSettings::pivot)
            .field("snap", &GizmoSettings::snap)
            .field("ruler", &GizmoSettings::ruler);
        reflectStruct<GizmoToolParams>("editor.GizmoToolParams", 1, structs)
            .field("tool", &GizmoToolParams::tool);
        reflectStruct<GizmoSpaceParams>("editor.GizmoSpaceParams", 1, structs)
            .field("space", &GizmoSpaceParams::space);
        reflectStruct<GizmoPivotParams>("editor.GizmoPivotParams", 1, structs)
            .field("pivot", &GizmoPivotParams::pivot);
        reflectStruct<GizmoSnapParams>("editor.GizmoSnapParams", 1, structs)
            .field("enabled", &GizmoSnapParams::enabled)
            .field("translate", &GizmoSnapParams::translate, grid)
            .field("rotate", &GizmoSnapParams::rotate, snapAngle)
            .field("scale", &GizmoSnapParams::scale, snapScale)
            .field("alignToNormal", &GizmoSnapParams::alignToNormal);
        reflectStruct<GizmoMeasureParams>("editor.GizmoMeasureParams", 1, structs)
            .field("from", &GizmoMeasureParams::from)
            .field("to", &GizmoMeasureParams::to)
            .field("entities", &GizmoMeasureParams::entities)
            .field("show", &GizmoMeasureParams::show);
        reflectStruct<Measurement>("editor.Measurement", 1, structs)
            .field("from", &Measurement::from)
            .field("to", &Measurement::to)
            .field("delta", &Measurement::delta)
            .field("distance", &Measurement::distance)
            .field("horizontal", &Measurement::horizontal)
            .field("elevation", &Measurement::elevation, {.unit = Unit::Degrees, .min = -90.0, .max = 90.0});
        reflectStruct<GizmoModalParams>("editor.GizmoModalParams", 1, structs)
            .field("operation", &GizmoModalParams::operation);

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
            .field("dependencies", &AssetInfoResult::dependencies)
            .field("importSettings", &AssetInfoResult::importSettings);
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
        reflectStruct<AssetFindMissingResult>("editor.AssetFindMissingResult", 1, structs)
            .field("references", &AssetFindMissingResult::references)
            .field("assets", &AssetFindMissingResult::assets);
        reflectStruct<AssetSetImportSettingsParams>("editor.AssetSetImportSettingsParams", 1, structs)
            .field("path", &AssetSetImportSettingsParams::path)
            .field("value", &AssetSetImportSettingsParams::value);
        reflectStruct<AssetImportSettingsResult>("editor.AssetImportSettingsResult", 1, structs)
            .field("path", &AssetImportSettingsResult::path)
            .field("sidecar", &AssetImportSettingsResult::sidecar)
            .field("value", &AssetImportSettingsResult::value)
            .field("changed", &AssetImportSettingsResult::changed);
        reflectStruct<AssetReimportResult>("editor.AssetReimportResult", 1, structs)
            .field("assets", &AssetReimportResult::assets);

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

        const FieldMeta steps{.min = 1.0, .max = 100000.0};
        reflectStruct<PlayStartParams>("editor.PlayStartParams", 1, structs)
            .field("mode", &PlayStartParams::mode)
            .field("fixedStep", &PlayStartParams::fixedStep)
            .field("seed", &PlayStartParams::seed);
        reflectStruct<PlayStatusResult>("editor.PlayStatusResult", 1, structs)
            .field("state", &PlayStatusResult::state)
            .field("mode", &PlayStatusResult::mode)
            .field("fixedStep", &PlayStatusResult::fixedStep)
            .field("timeScale", &PlayStatusResult::timeScale)
            .field("steps", &PlayStatusResult::steps)
            .field("time", &PlayStatusResult::time, {.unit = Unit::Seconds});
        reflectStruct<PlayPauseParams>("editor.PlayPauseParams", 1, structs)
            .field("paused", &PlayPauseParams::paused);
        reflectStruct<PlayStepParams>("editor.PlayStepParams", 1, structs)
            .field("steps", &PlayStepParams::steps, steps);
        reflectStruct<PlayTimeScaleParams>("editor.PlayTimeScaleParams", 1, structs)
            .field("scale", &PlayTimeScaleParams::scale, {.min = 0.0, .max = 100.0});
        reflectStruct<PlayRunUntilParams>("editor.PlayRunUntilParams", 1, structs)
            .field("maxSteps", &PlayRunUntilParams::maxSteps, steps)
            .field("entity", &PlayRunUntilParams::entity)
            .field("exists", &PlayRunUntilParams::exists)
            .field("component", &PlayRunUntilParams::component)
            .field("field", &PlayRunUntilParams::field)
            .field("equals", &PlayRunUntilParams::equals);
        reflectStruct<PlayRunUntilResult>("editor.PlayRunUntilResult", 1, structs)
            .field("met", &PlayRunUntilResult::met)
            .field("steps", &PlayRunUntilResult::steps)
            .field("status", &PlayRunUntilResult::status);
        reflectStruct<PlayKeepChangesParams>("editor.PlayKeepChangesParams", 1, structs)
            .field("entities", &PlayKeepChangesParams::entities)
            .field("components", &PlayKeepChangesParams::components);
        reflectStruct<PlayKeepChangesResult>("editor.PlayKeepChangesResult", 1, structs)
            .field("entities", &PlayKeepChangesResult::entities)
            .field("components", &PlayKeepChangesResult::components);

        reflectStruct<ModuleLoadParams>("editor.ModuleLoadParams", 1, structs)
            .field("path", &ModuleLoadParams::path);
        reflectStruct<ModuleUnloadParams>("editor.ModuleUnloadParams", 1, structs)
            .field("name", &ModuleUnloadParams::name);
        reflectStruct<PlayComponentMigration>("editor.ComponentMigration", 1, structs)
            .field("type", &PlayComponentMigration::type)
            .field("components", &PlayComponentMigration::components)
            .field("addedFields", &PlayComponentMigration::addedFields)
            .field("removedFields", &PlayComponentMigration::removedFields)
            .field("resetFields", &PlayComponentMigration::resetFields);
        reflectStruct<PlayModuleInfo>("editor.ModuleInfo", 1, structs)
            .field("name", &PlayModuleInfo::name)
            .field("version", &PlayModuleInfo::version)
            .field("path", &PlayModuleInfo::path)
            .field("shared", &PlayModuleInfo::shared)
            .field("reloads", &PlayModuleInfo::reloads)
            .field("migrated", &PlayModuleInfo::migrated);
        reflectStruct<ModuleListResult>("editor.ModuleListResult", 1, structs)
            .field("modules", &ModuleListResult::modules);
        reflectStruct<ModuleCreateParams>("editor.ModuleCreateParams", 1, structs)
            .field("name", &ModuleCreateParams::name);
        reflectStruct<ModuleCreateResult>("editor.ModuleCreateResult", 1, structs)
            .field("directory", &ModuleCreateResult::directory)
            .field("files", &ModuleCreateResult::files);
        reflectStruct<ModuleBuildParams>("editor.ModuleBuildParams", 1, structs)
            .field("modules", &ModuleBuildParams::modules)
            .field("reconfigure", &ModuleBuildParams::reconfigure)
            .field("load", &ModuleBuildParams::load);
        reflectStruct<ModuleDiagnostic>("editor.ModuleDiagnostic", 1, structs)
            .field("file", &ModuleDiagnostic::file)
            .field("line", &ModuleDiagnostic::line)
            .field("column", &ModuleDiagnostic::column)
            .field("severity", &ModuleDiagnostic::severity)
            .field("code", &ModuleDiagnostic::code)
            .field("message", &ModuleDiagnostic::message);
        reflectStruct<ModuleBuildResult>("editor.ModuleBuildResult", 1, structs)
            .field("succeeded", &ModuleBuildResult::succeeded)
            .field("configured", &ModuleBuildResult::configured)
            .field("errors", &ModuleBuildResult::errors)
            .field("warnings", &ModuleBuildResult::warnings)
            .field("diagnostics", &ModuleBuildResult::diagnostics)
            .field("libraries", &ModuleBuildResult::libraries)
            .field("loaded", &ModuleBuildResult::loaded)
            .field("reloaded", &ModuleBuildResult::reloaded)
            .field("modules", &ModuleBuildResult::modules)
            .field("output", &ModuleBuildResult::output);
        reflectStruct<OpenSourceParams>("editor.OpenSourceParams", 1, structs)
            .field("path", &OpenSourceParams::path)
            .field("line", &OpenSourceParams::line)
            .field("column", &OpenSourceParams::column);
        reflectStruct<OpenSourceResult>("editor.OpenSourceResult", 1, structs)
            .field("program", &OpenSourceResult::program)
            .field("arguments", &OpenSourceResult::arguments);
        reflectStruct<ModuleReloadParams>("editor.ModuleReloadParams", 1, structs)
            .field("name", &ModuleReloadParams::name);
        reflectStruct<ModuleReloadResult>("editor.ModuleReloadResult", 1, structs)
            .field("reloaded", &ModuleReloadResult::reloaded)
            .field("modules", &ModuleReloadResult::modules);

        reflectStruct<InputAssetParams>("editor.InputAssetParams", 1, structs)
            .field("asset", &InputAssetParams::asset);
        reflectStruct<InputActionInfo>("editor.InputActionInfo", 1, structs)
            .field("name", &InputActionInfo::name)
            .field("type", &InputActionInfo::type)
            .field("bindings", &InputActionInfo::bindings);
        reflectStruct<InputActionsResult>("editor.InputActionsResult", 1, structs)
            .field("asset", &InputActionsResult::asset)
            .field("actions", &InputActionsResult::actions);
        reflectStruct<InputActionDefineParams>("editor.InputActionDefineParams", 1, structs)
            .field("asset", &InputActionDefineParams::asset)
            .field("name", &InputActionDefineParams::name)
            .field("type", &InputActionDefineParams::type)
            .field("bindings", &InputActionDefineParams::bindings);
        reflectStruct<InputActionParams>("editor.InputActionParams", 1, structs)
            .field("asset", &InputActionParams::asset)
            .field("name", &InputActionParams::name);
        reflectStruct<InputActionRenameParams>("editor.InputActionRenameParams", 1, structs)
            .field("asset", &InputActionRenameParams::asset)
            .field("name", &InputActionRenameParams::name)
            .field("newName", &InputActionRenameParams::newName);
        reflectStruct<InputBindParams>("editor.InputBindParams", 1, structs)
            .field("asset", &InputBindParams::asset)
            .field("action", &InputBindParams::action)
            .field("binding", &InputBindParams::binding);
        reflectStruct<InputKeyEvent>("editor.InputKeyEvent", 1, structs)
            .field("key", &InputKeyEvent::key)
            .field("down", &InputKeyEvent::down);
        reflectStruct<InputButtonEvent>("editor.InputButtonEvent", 1, structs)
            .field("button", &InputButtonEvent::button)
            .field("down", &InputButtonEvent::down);
        reflectStruct<InputInjectParams>("editor.InputInjectParams", 1, structs)
            .field("keys", &InputInjectParams::keys)
            .field("mouseButtons", &InputInjectParams::mouseButtons)
            .field("mousePosition", &InputInjectParams::mousePosition);
        reflectStruct<InputReadParams>("editor.InputReadParams", 1, structs)
            .field("actions", &InputReadParams::actions);
        reflectStruct<PlayActionState>("editor.PlayActionState", 1, structs)
            .field("name", &PlayActionState::name)
            .field("type", &PlayActionState::type)
            .field("down", &PlayActionState::down)
            .field("pressed", &PlayActionState::pressed)
            .field("released", &PlayActionState::released)
            .field("value", &PlayActionState::value);
        reflectStruct<InputReadResult>("editor.InputReadResult", 1, structs)
            .field("actions", &InputReadResult::actions);
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
    detail::registerGizmoCommands(registry);
    detail::registerSelectionCommands(registry);
    detail::registerViewCommands(registry);
    detail::registerViewportCommands(registry);
    detail::registerHistoryCommands(registry);
    detail::registerTransactionCommands(registry);
    detail::registerClipboardCommands(registry);
    detail::registerAssetCommands(registry);
    detail::registerMaterialCommands(registry);
    detail::registerLogCommands(registry);
    detail::registerSettingsCommands(registry);
    detail::registerUiCommands(registry);
    detail::registerPlayCommands(registry);
    detail::registerInputCommands(registry);
    detail::registerModuleCommands(registry);
    detail::registerToolCommands(registry);
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
