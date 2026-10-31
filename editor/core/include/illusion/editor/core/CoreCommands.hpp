#pragma once

/// @file
/// The core command modules (`editor`, `project`, `scene`, `entity`, `component`, `hierarchy`, `transform`,
/// `selection`, `history`, `clipboard`, `asset`, `material` and `log`) and their params and result structs,
/// for typed calls from the UI. The generated reference lists every command:
/// docs/content/en/reference/editor-commands.md.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/editor/core/Arrange.hpp>
#include <illusion/editor/core/EntityIndex.hpp>
#include <illusion/editor/core/Gizmo.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/Snap.hpp>
#include <illusion/editor/core/UiHost.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace illusion
{
class StructRegistry;
} // namespace illusion

namespace illusion::editor
{

class CommandRegistry;

/// Reflects the params and result structs below in @p structs (once; later calls do nothing).
void reflectCoreCommandTypes(StructRegistry& structs);

/// Registers the core commands in @p registry (reflecting their types first).
void registerCoreCommands(CommandRegistry& registry);

// --- editor ------------------------------------------------------------------------------------------------

/// Params of `editor.list_commands`.
struct ListCommandsParams
{
    std::optional<std::string> category{}; ///< Only this category, e.g. "Entity".
    std::optional<std::string> query{};    ///< Only names that fuzzy-match this text.
};

/// One command in a listing.
struct CommandSummary
{
    std::string name;               ///< e.g. "entity.create".
    u32 version = 1;                ///< Command version.
    std::string title;              ///< Localized title.
    std::string summary;            ///< English summary for agents.
    std::string category;           ///< Category.
    std::string permission;         ///< Permission class, e.g. "edit".
    std::vector<std::string> flags; ///< Set flags: "read_only", "destructive", "idempotent", "undoable", ...
};

/// Result of `editor.list_commands`.
struct ListCommandsResult
{
    std::vector<CommandSummary> commands; ///< Sorted by name.
};

/// Params of `editor.describe`.
struct DescribeParams
{
    std::string command; ///< Command name.
};

/// Result of `editor.describe`.
struct DescribeResult
{
    CommandSummary command;  ///< The command.
    std::string description; ///< Localized description.
    std::string icon;        ///< Icon name.
    std::string mode;        ///< "edit", "play" or "any".
    std::string shortcut;    ///< Default shortcut ("" for none).
    JsonText params;         ///< JSON Schema of the params.
    JsonText result;         ///< JSON Schema of the result.
};

/// One step of `editor.batch`.
struct BatchStep
{
    /// Name under which later steps refer to its result: the string "$name.path.to.field" in their params is
    /// replaced by that value of the result (array elements by index). Letters, digits and '_'.
    std::optional<std::string> as{};
    std::string method;               ///< The command, e.g. "entity.create".
    std::optional<JsonText> params{}; ///< Its params (an object; default {}).
};

/// Params of `editor.batch`.
struct BatchParams
{
    std::optional<std::string> label{}; ///< Label of the undo step (default "Batch").
    /// True (default): the first failing step reverts the whole batch and fails it. False: failing steps are
    /// reported and the others are kept.
    std::optional<bool> atomic{};
    std::vector<BatchStep> steps; ///< The commands, run in order (1 to 1,000).
};

/// Outcome of one step of `editor.batch`.
struct BatchStepResult
{
    std::optional<JsonText> result{}; ///< Its result, with `_editor`, if it succeeded.
    std::optional<JsonText> error{};  ///< Its error {code, message, data} if it failed.
};

/// Result of `editor.batch`.
struct BatchResult
{
    std::vector<BatchStepResult> steps; ///< One entry per step, in order.
    u32 failed = 0;                     ///< Steps that failed (non-atomic batches only).
};

/// Params of `editor.wait`.
struct WaitParams
{
    u32 frames = 1; ///< Editor frames to wait (1 to 100,000).
};

/// Result of `editor.wait`.
struct WaitResult
{
    u32 frames = 0; ///< Frames waited.
};

// --- project -----------------------------------------------------------------------------------------------

/// Result of `project.info`.
struct ProjectInfoResult
{
    std::string name;                 ///< Project name.
    std::string version;              ///< Game version.
    std::string engineVersion;        ///< Engine version in the manifest.
    std::string root;                 ///< Project directory (absolute, UTF-8).
    std::string startupScene;         ///< Startup scene ("" for none).
    std::vector<std::string> modules; ///< Game modules.
    std::vector<std::string> mounts;  ///< Mounts, e.g. "assets:/".
};

/// Params with one virtual path.
struct FilePathParams
{
    std::string path; ///< Virtual path, e.g. "assets:/scenes/main.scene".
};

/// Params of `project.file_list`.
struct FileListParams
{
    std::string path;                ///< Directory.
    std::optional<bool> recursive{}; ///< Include everything below it (default false).
};

/// One file or directory.
struct FileEntry
{
    std::string path;         ///< Virtual path.
    bool isDirectory = false; ///< True for directories.
    u64 size = 0;             ///< Size in bytes (0 for directories).
};

/// Result of `project.file_list`.
struct FileListResult
{
    std::vector<FileEntry> entries; ///< Sorted by path.
};

/// Result of `project.file_read`.
struct FileReadResult
{
    std::string path; ///< Virtual path.
    std::string text; ///< Contents (UTF-8).
};

/// Result of `project.file_stat`.
struct FileStatResult
{
    std::string path;         ///< Virtual path.
    bool exists = false;      ///< False if nothing is there.
    bool isDirectory = false; ///< True for directories.
    u64 size = 0;             ///< Size in bytes.
};

/// Params of `project.file_write`.
struct FileWriteParams
{
    std::string path; ///< Virtual path of the file.
    std::string text; ///< New contents (UTF-8).
};

/// Result of `project.file_write`.
struct FileWriteResult
{
    std::string path;     ///< Virtual path.
    bool created = false; ///< True if the file did not exist.
};

/// Params of `project.file_move`.
struct FileMoveParams
{
    std::string from; ///< Current virtual path.
    std::string to;   ///< New virtual path.
};

/// Result with one virtual path.
struct FilePathResult
{
    std::string path; ///< Virtual path.
};

/// Params of `project.create`.
struct ProjectCreateParams
{
    std::string directory; ///< New project directory, relative to the workspace, e.g. "tetris".
    std::string name;      ///< Display name.
    std::optional<std::string> templateName{}; ///< Template (default "empty"), e.g. "3d-starter".
    std::optional<bool> discardChanges{};      ///< Close a scene with unsaved changes (default false).
};

/// Params of `project.open`.
struct ProjectOpenParams
{
    std::string directory;                ///< Project directory, relative to the workspace.
    std::optional<bool> discardChanges{}; ///< Close a scene with unsaved changes (default false).
};

/// Params of `project.recover`.
struct RecoverParams
{
    std::optional<std::string> journal{}; ///< Journal file name in `.illusion/journal` (default: newest).
};

/// Result of `project.recover`.
struct RecoverResult
{
    std::string journal; ///< Replayed journal file name.
    std::string scene;   ///< Scene it started from ("" for a new scene).
    u32 operations = 0;  ///< Operations applied (one undo step).
};

/// Params of `project.settings_get` and `scene.settings_get`.
struct SettingsGetParams
{
    std::optional<std::string> type{}; ///< Only this settings type (default: every one).
};

/// One settings type and its value.
struct SettingsEntry
{
    std::string type;     ///< Stable name of the settings struct.
    bool known = true;    ///< False for settings of a scene whose type is not registered (kept as is).
    bool present = false; ///< False if the scene or project does not set it (the value is the default).
    u32 version = 0;      ///< Data version of the value (0 if an unknown type names none).
    std::string file;     ///< Project settings: the file, e.g. "settings:/controls.json" ("" for scenes).
    JsonText value{"{}"}; ///< The value: every field for known types, as saved for unknown ones.
};

/// Result of `project.settings_get` and `scene.settings_get`.
struct SettingsGetResult
{
    std::vector<SettingsEntry> settings; ///< Sorted by type.
};

/// Params of `project.settings_set` and `scene.settings_set`.
struct SettingsSetParams
{
    std::string type;              ///< Stable name of the settings struct.
    JsonText value;                ///< Merge patch of fields, or null to remove the settings (defaults).
    std::optional<bool> replace{}; ///< Reset the fields the value does not mention (default false).
};

/// Result of `project.settings_set` and `scene.settings_set`.
struct SettingsSetResult
{
    std::string type;     ///< Stable name of the settings struct.
    bool changed = false; ///< False if the settings were already like that.
    bool present = false; ///< False if the settings were removed.
    JsonText value{"{}"}; ///< The value afterwards (every field).
};

// --- scene -------------------------------------------------------------------------------------------------

/// Params of `scene.new`.
struct SceneNewParams
{
    std::optional<bool> discardChanges{}; ///< Allow losing unsaved changes (default false).
};

/// Params of `scene.open`.
struct SceneOpenParams
{
    std::string path;                     ///< Scene file.
    std::optional<bool> discardChanges{}; ///< Allow losing unsaved changes (default false).
};

/// Params of `scene.save_as`.
struct SceneSaveAsParams
{
    std::string path;                ///< New scene file.
    std::optional<bool> overwrite{}; ///< Replace an existing file (default false).
};

/// Facts about the open scene.
struct SceneInfoResult
{
    std::string path;   ///< Scene file ("" if untitled).
    std::string name;   ///< Display name.
    u64 revision = 0;   ///< Scene revision.
    bool dirty = false; ///< Unsaved changes.
    u32 entities = 0;   ///< Number of entities.
    u32 roots = 0;      ///< Number of root entities.
    std::string
        hash{}; ///< xxhash64 of the scene's scene v2 JSON as 16 hex digits (equal content, equal hash).
};

/// Result of `scene.save` and `scene.save_as`.
struct SceneFileResult
{
    std::string path; ///< Scene file.
    u32 entities = 0; ///< Saved entities.
};

/// Params of `scene.tree`.
struct SceneTreeParams
{
    std::optional<std::string> root{}; ///< Entity whose subtree to list (default: the whole scene).
    std::optional<u32> depth{};        ///< Levels below the start to include (default: all).
};

/// One entity of a tree listing.
struct TreeNode
{
    std::string id;      ///< Entity id.
    std::string name;    ///< Name.
    std::string path;    ///< Hierarchy path.
    u32 depth = 0;       ///< Level below the start (0 for roots or the given root).
    u32 children = 0;    ///< Number of children.
    bool enabled = true; ///< False if it has the Disabled tag.
};

/// Result of `scene.tree`.
struct SceneTreeResult
{
    std::vector<TreeNode> nodes; ///< In pre-order: every node is followed by its descendants.
};

/// Params of `scene.query`.
struct SceneQueryParams
{
    std::optional<std::vector<std::string>> has{};     ///< Components the entities must have (stable names).
    std::optional<std::vector<std::string>> without{}; ///< Components they must not have.
    std::optional<std::string> name{};   ///< Name pattern with `*` and `?` wildcards, case-sensitive.
    std::optional<std::string> under{};  ///< Only descendants of this entity.
    std::optional<u32> limit{};          ///< Most results to return (default 100, at most 10000).
    std::optional<std::string> cursor{}; ///< `next` of the previous page.
};

/// Result of `scene.query` and `entity.find`.
struct SceneQueryResult
{
    std::vector<EntitySummary> entities; ///< Matches in hierarchy order.
    u32 total = 0;                       ///< Matches in all pages.
    std::optional<std::string> next{};   ///< Cursor of the next page (null on the last page).
};

// --- entity ------------------------------------------------------------------------------------------------

/// Params of `entity.find`.
struct EntityFindParams
{
    std::string name;           ///< Name pattern with `*` and `?` wildcards, case-sensitive.
    std::optional<u32> limit{}; ///< Most results to return (default 100, at most 10000).
};

/// Params of `entity.create`.
struct EntityCreateParams
{
    std::optional<std::string> name{};    ///< Name.
    std::optional<std::string> parent{};  ///< Parent entity (default: a root).
    std::optional<u32> index{};           ///< Position among the siblings (default: last).
    std::optional<JsonText> components{}; ///< Object: stable Component name -> value.
};

/// Result with one entity.
struct EntityResult
{
    EntitySummary entity; ///< The entity.
};

/// Params with a list of entities.
struct EntitiesParams
{
    std::vector<std::string> entities; ///< Entity references (`$selection` for the selection).
};

/// Result of `entity.destroy`.
struct EntityDestroyResult
{
    u32 destroyed = 0; ///< Destroyed entities, descendants included.
};

/// Result with a list of entities.
struct EntitiesResult
{
    std::vector<EntitySummary> entities; ///< The entities.
};

/// Params of `entity.rename`.
struct EntityRenameParams
{
    std::string entity; ///< Entity reference.
    std::string name;   ///< New name ("" removes it).
};

/// Params of `entity.set_enabled`.
struct EntitySetEnabledParams
{
    std::string entity;  ///< Entity reference.
    bool enabled = true; ///< False adds the Disabled tag.
};

/// Params of `entity.get`.
struct EntityGetParams
{
    std::string entity;                                   ///< Entity reference.
    std::optional<std::vector<std::string>> components{}; ///< Only these Components (default: all).
};

/// Result of `entity.get`.
struct EntityGetResult
{
    EntitySummary entity;                ///< The entity.
    bool enabled = true;                 ///< False if it has the Disabled tag.
    std::optional<std::string> parent{}; ///< Id of the parent (null for roots).
    u32 children = 0;                    ///< Number of children.
    JsonText components{"{}"};           ///< Object: stable Component name -> value.
};

// --- component ---------------------------------------------------------------------------------------------

/// Params of `component.types`.
struct ComponentTypesParams
{
    std::optional<std::string> category{}; ///< Only this category (or its subcategories).
};

/// One Component type.
struct ComponentTypeInfo
{
    std::string name;        ///< Stable name.
    std::string displayName; ///< English display name.
    std::string category;    ///< Category, e.g. "Physics/Bodies".
    std::string icon;        ///< Icon name.
    std::string description; ///< English description.
    bool tag = false;        ///< True for tags (no fields).
    bool runtime = false;    ///< Runtime state, not saved in scenes.
};

/// Result of `component.types`.
struct ComponentTypesResult
{
    std::vector<ComponentTypeInfo> types; ///< Sorted by name.
};

/// Params with one Component type.
struct ComponentTypeParams
{
    std::string type; ///< Stable name.
};

/// Result of `component.schema`.
struct ComponentSchemaResult
{
    std::string type; ///< Stable name.
    u32 version = 1;  ///< Data version.
    JsonText schema;  ///< JSON Schema of its value.
};

/// Params of `component.add`.
struct ComponentAddParams
{
    std::string entity;              ///< Entity reference.
    std::string type;                ///< Stable name.
    std::optional<JsonText> value{}; ///< Fields to set after adding (merge).
};

/// Result of `component.add`.
struct ComponentAddResult
{
    EntitySummary entity;           ///< The entity.
    std::vector<std::string> added; ///< Added Components: required ones first, then the type.
};

/// Params of `component.remove` and `component.copy`.
struct ComponentRefParams
{
    std::string entity; ///< Entity reference.
    std::string type;   ///< Stable name.
};

/// Params of `component.get`.
struct ComponentGetParams
{
    std::string entity;                ///< Entity reference.
    std::string type;                  ///< Stable name.
    std::optional<std::string> path{}; ///< Only this field, e.g. "position/x".
};

/// Result of `component.get`.
struct ComponentGetResult
{
    JsonText value; ///< The value (or the field's value).
};

/// Params of `component.set`.
struct ComponentSetParams
{
    std::string entity; ///< Entity reference.
    std::string type;   ///< Stable name.
    JsonText value;     ///< Merge patch: fields to change.
};

/// Params of `component.set_field`.
struct ComponentSetFieldParams
{
    std::string entity; ///< Entity reference.
    std::string type;   ///< Stable name.
    std::string path;   ///< Field path, e.g. "position/x".
    JsonText value;     ///< New value.
};

/// Result of commands that change a value.
struct ComponentChangeResult
{
    EntitySummary entity; ///< The entity.
    bool changed = false; ///< False if the value was already like that.
};

/// Params of `component.reset`.
struct ComponentResetParams
{
    std::string entity;                ///< Entity reference.
    std::string type;                  ///< Stable name.
    std::optional<std::string> path{}; ///< Only this field, e.g. "position/x" (default: the whole value).
};

/// Result of `component.copy`.
struct ComponentCopyResult
{
    std::string type; ///< Stable name of the copied type.
    JsonText value;   ///< The copied value.
};

/// Result of `component.paste`.
struct ComponentPasteResult
{
    std::string type;                    ///< Stable name of the pasted type.
    std::vector<EntitySummary> entities; ///< The entities that got the value.
    u32 added = 0;                       ///< How many of them did not have the Component before.
};

// --- hierarchy ---------------------------------------------------------------------------------------------

/// Params of `hierarchy.set_parent`.
struct SetParentParams
{
    std::string entity;                  ///< Entity reference.
    std::optional<std::string> parent{}; ///< New parent (null or absent: make it a root).
    std::optional<u32> index{};          ///< Position among the new siblings (default: last).
    std::optional<bool> keepWorld{};     ///< Keep the world transform (default true) or the local one.
};

/// Params of `hierarchy.reorder`.
struct ReorderParams
{
    std::string entity; ///< Entity reference.
    u32 index = 0;      ///< New position among its siblings.
};

/// Params of `hierarchy.children`.
struct ChildrenParams
{
    std::optional<std::string> entity{}; ///< Parent (default: the roots).
};

// --- transform ---------------------------------------------------------------------------------------------

/// Coordinate space of transform commands.
enum class TransformSpace : u8
{
    World, ///< World space.
    Local, ///< The parent's space (world space for roots).
    Self,  ///< The entity's own axes.
};

/// Point that `transform.rotate` and `transform.scale` turn or scale entities about.
enum class TransformPivot : u8
{
    Individual, ///< Each entity about its own position.
    Center,     ///< All about the mean of their world positions.
};

/// Params of `transform.get`.
struct TransformGetParams
{
    std::string entity;                    ///< Entity reference.
    std::optional<TransformSpace> space{}; ///< "world" (default) or "local".
};

/// Result of `transform.get`: a placement with the rotation also as Euler angles.
struct TransformGetResult
{
    EntitySummary entity;                  ///< The entity.
    Vec3 position{0.0f};                   ///< Position.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Rotation [x, y, z, w].
    Vec3 euler{0.0f};                      ///< The rotation as Euler angles in degrees (x, y, z).
    Vec3 scale{1.0f};                      ///< Scale per axis.
};

/// Params of `transform.set`.
struct TransformSetParams
{
    std::vector<std::string> entities;     ///< Entity references (`$selection` for the selection).
    std::optional<TransformSpace> space{}; ///< "world" (default) or "local".
    std::optional<Vec3> position{};        ///< New position.
    std::optional<Quat> rotation{};        ///< New rotation [x, y, z, w] (not with euler).
    std::optional<Vec3> euler{};           ///< New rotation as Euler angles in degrees (not with rotation).
    std::optional<Vec3> scale{};           ///< New scale (local: relative to the parent's).
};

/// Params of `transform.translate`.
struct TransformTranslateParams
{
    std::vector<std::string>
        entities;      ///< Entity references; descendants of other listed entities follow them.
    Vec3 offset{0.0f}; ///< Distance to move.
    std::optional<TransformSpace> space{}; ///< Axes of the offset: "world" (default), "local" or "self".
};

/// Params of `transform.rotate`.
struct TransformRotateParams
{
    std::vector<std::string>
        entities;                   ///< Entity references; descendants of other listed entities follow them.
    std::optional<Quat> rotation{}; ///< Rotation to apply [x, y, z, w] (or euler).
    std::optional<Vec3> euler{};    ///< Rotation to apply as Euler angles in degrees (or rotation).
    std::optional<TransformSpace> space{}; ///< Axes of the rotation: "world" (default), "local" or "self".
    std::optional<TransformPivot> pivot{}; ///< "individual" (default) or "center".
    std::optional<Vec3> point{};           ///< A world-space pivot point instead of pivot.
};

/// Params of `transform.scale`.
struct TransformScaleParams
{
    std::vector<std::string>
        entities;      ///< Entity references; descendants of other listed entities follow them.
    Vec3 factor{1.0f}; ///< Factor per axis of the entities' own scale.
    std::optional<TransformPivot> pivot{}; ///< "individual" (default) or "center".
    std::optional<Vec3> point{};           ///< A world-space pivot point instead of pivot.
};

/// Params of `transform.reset`.
struct TransformResetParams
{
    std::vector<std::string> entities; ///< Entity references.
    std::optional<bool> position{};    ///< Reset the position (default true).
    std::optional<bool> rotation{};    ///< Reset the rotation (default true).
    std::optional<bool> scale{};       ///< Reset the scale (default true).
};

/// Params of `transform.snap_to_grid`.
struct TransformSnapParams
{
    std::vector<std::string>
        entities;                   ///< Entity references; descendants of other listed entities follow them.
    std::optional<f32> increment{}; ///< Grid spacing in world units (default: the gizmo's translation snap).
};

/// An axis of `transform.align` and `transform.distribute`.
enum class TransformAxis : u8
{
    X, ///< The x axis.
    Y, ///< The y axis.
    Z, ///< The z axis.
};

/// Params of `transform.align`.
struct TransformAlignParams
{
    std::vector<std::string>
        entities; ///< Entity references; descendants of other listed entities follow them.
    TransformAxis axis = TransformAxis::X; ///< The axis to align along.
    std::optional<AlignEdge> edge{};       ///< Edge of the boxes: "min", "center" (default) or "max".
    std::optional<AlignTarget> to{}; ///< "selection" (default: the box around all) or "active" (the last).
};

/// Params of `transform.distribute`.
struct TransformDistributeParams
{
    std::vector<std::string> entities;     ///< Entity references (three or more after dropping descendants).
    TransformAxis axis = TransformAxis::X; ///< The axis to space them along.
    std::optional<DistributeMode> mode{};  ///< "centers" (default) or "gaps".
};

/// Params of `transform.drop_to_ground`.
struct TransformDropParams
{
    std::vector<std::string>
        entities; ///< Entity references; descendants of other listed entities follow them.
    std::optional<DropTarget> onto{}; ///< "surfaces" (default) or "plane".
};

/// Params of `transform.align_to_view`.
struct TransformAlignViewParams
{
    std::vector<std::string>
        entities; ///< Entity references; descendants of other listed entities follow them.
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    std::optional<bool> move{};            ///< Also move them to the viewport's eye (default false).
};

// --- gizmo -------------------------------------------------------------------------------------------------

/// Params of `gizmo.set_tool`.
struct GizmoToolParams
{
    GizmoTool tool = GizmoTool::Translate; ///< The tool.
};

/// Params of `gizmo.set_space`.
struct GizmoSpaceParams
{
    GizmoSpace space = GizmoSpace::World; ///< The axes.
};

/// Params of `gizmo.set_pivot`.
struct GizmoPivotParams
{
    GizmoPivot pivot = GizmoPivot::Median; ///< The pivot.
};

/// Params of `gizmo.set_snap` (absent members are unchanged).
struct GizmoSnapParams
{
    std::optional<bool> enabled{};       ///< Snap without Ctrl.
    std::optional<f32> translate{};      ///< Distance increment in world units.
    std::optional<f32> rotate{};         ///< Angle increment in degrees.
    std::optional<f32> scale{};          ///< Scale factor increment.
    std::optional<bool> alignToNormal{}; ///< Surface snapping turns the entities' +y axis onto the normal.
};

/// Params of `gizmo.measure`: two points, or two entities instead.
struct GizmoMeasureParams
{
    std::optional<Vec3> from{}; ///< Start in world space (with to).
    std::optional<Vec3> to{};   ///< End in world space (with from).
    /// Two entity references instead of from and to: measures between their world positions.
    std::optional<std::vector<std::string>> entities{};
    std::optional<bool> show{}; ///< Show the ruler in the Scene viewports (default true).
};

/// Params of `gizmo.modal`.
struct GizmoModalParams
{
    GizmoModalOperation operation = GizmoModalOperation::Translate; ///< What the modal transform does.
};

// --- history -----------------------------------------------------------------------------------------------

/// Params of `history.redo`.
struct HistoryStepParams
{
    std::optional<u32> steps{}; ///< Number of steps (default 1).
};

/// Params of `history.undo`.
struct HistoryUndoParams
{
    std::optional<u32> steps{}; ///< Number of steps (default 1).
    /// "all" (default): the last steps, whoever made them; "mine": the last steps of the caller's origin,
    /// even when steps of others came later (refused with a conflict when those touched the same things).
    std::optional<std::string> scope{};
};

// --- transaction -------------------------------------------------------------------------------------------

/// Params of `transaction.begin`.
struct TransactionBeginParams
{
    std::optional<std::string> label{}; ///< Label of the undo step it becomes (default "Transaction").
};

/// Result of `transaction.begin`.
struct TransactionBeginResult
{
    std::string label; ///< Label of the undo step it becomes.
};

/// Result of `transaction.commit` and `transaction.cancel`.
struct TransactionEndResult
{
    u32 commands = 0;   ///< Commands that changed something in the transaction.
    u32 operations = 0; ///< Primitive operations they applied (and a cancel reverted).
    u32 position = 0;   ///< History position afterwards.
    u64 revision = 0;   ///< Scene revision afterwards.
};

/// Result of history commands.
struct HistoryResult
{
    u32 steps = 0;    ///< Steps undone, redone or removed.
    u32 position = 0; ///< History position afterwards.
    u64 revision = 0; ///< Scene revision afterwards.
};

/// Params of `history.jump`.
struct HistoryJumpParams
{
    u32 position = 0; ///< Target position (0: before the first step).
};

/// One step in the history listing.
struct HistoryEntry
{
    u32 index = 0;        ///< Position after it is applied minus one.
    std::string label;    ///< Label.
    std::string origin;   ///< Origin.
    bool applied = false; ///< False for undone steps (redo applies them).
};

/// Result of `history.list`.
struct HistoryListResult
{
    std::vector<HistoryEntry> steps; ///< Oldest first.
    u32 position = 0;                ///< Number of applied steps.
    u64 bytes = 0;                   ///< Memory used.
    u64 budget = 0;                  ///< Memory budget.
};

// --- selection ---------------------------------------------------------------------------------------------

/// Result of selection commands.
struct SelectionResult
{
    std::vector<EntitySummary> entities; ///< Selected entities, the primary last.
};

// --- view --------------------------------------------------------------------------------------------------

/// Params of `view.set_entities`.
struct ViewSetParams
{
    std::vector<std::string> entities; ///< Entity references (`$selection` for the selection).
    std::optional<bool> hidden{};      ///< Hide (true) or show (false) them in viewports; absent: unchanged.
    std::optional<bool> locked{};      ///< Lock (true) or unlock (false) picking; absent: unchanged.
    std::optional<bool> recursive{};   ///< Also their descendants (default true).
};

/// Result of `view.get` and `view.set_entities`.
struct ViewStateResult
{
    std::vector<EntitySummary> hidden; ///< Hidden entities, by id.
    std::vector<EntitySummary> locked; ///< Locked entities, by id.
};

// --- viewport ----------------------------------------------------------------------------------------------

/// Params of commands that name a viewport (`viewport.camera_get`).
struct ViewportParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
};

/// A viewport in `viewport.list`.
struct ViewportEntry
{
    std::string id;                             ///< Stable id: "scene", "game", or "scene-2" to "scene-4".
    ViewportKind kind = ViewportKind::Scene;    ///< What it looks through.
    u32 width = 0;                              ///< Width of the image in pixels (0 before the first frame).
    u32 height = 0;                             ///< Height of the image in pixels.
    bool visible = false;                       ///< Drawn in the last frame.
    ViewportCamera camera;                      ///< The editor camera (Scene viewports).
    Vec3 eye{0.0f};                             ///< Position the image is seen from.
    ViewportSettings settings;                  ///< What it draws.
    std::optional<EntitySummary> sceneCamera{}; ///< The Camera a Game viewport looks through.
};

/// Result of `viewport.list`.
struct ViewportListResult
{
    std::vector<ViewportEntry> viewports; ///< Every viewport.
};

/// Result of the camera commands: the camera, or the camera it moves to when animated.
struct ViewportCameraResult
{
    std::string viewport;  ///< Viewport id.
    ViewportCamera camera; ///< The camera.
    Vec3 eye{0.0f};        ///< The camera's position.
    Vec3 forward{0.0f};    ///< Its unit view direction.
};

/// Params of `viewport.camera_set`: the fields to change.
struct ViewportCameraSetParams
{
    std::optional<std::string> viewport{};          ///< Viewport id (default "scene").
    std::optional<Vec3> pivot{};                    ///< Point to orbit around and look at.
    std::optional<f32> distance{};                  ///< Distance of the eye from the pivot.
    std::optional<f32> yaw{};                       ///< Degrees about +y.
    std::optional<f32> pitch{};                     ///< Degrees, -90 (down) to 90 (up).
    std::optional<f32> fieldOfView{};               ///< Vertical field of view in degrees.
    std::optional<ViewportProjection> projection{}; ///< Projection.
    std::optional<f32> orthographicHeight{};        ///< Vertical extent of orthographic views.
    std::optional<Vec3> eye{};                      ///< Put the eye here (keeps the pivot: aims at it).
    std::optional<bool> animate{};                  ///< Animate the move (default true).
};

/// Params of `viewport.view`.
struct ViewportViewParams
{
    std::optional<std::string> viewport{};         ///< Viewport id (default "scene").
    ViewportView view = ViewportView::Perspective; ///< The view.
    std::optional<bool> animate{};                 ///< Animate the move (default true).
};

/// Params of `viewport.frame`.
struct ViewportFrameParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    /// Entities to frame; absent: the selection, or everything when nothing is selected; []: everything.
    std::optional<std::vector<std::string>> entities{};
    std::optional<bool> animate{}; ///< Animate the move (default true).
};

/// Params of `viewport.set_mode`.
struct ViewportModeParams
{
    std::optional<std::string> viewport{};    ///< Viewport id (default "scene").
    std::optional<ViewportShading> shading{}; ///< Shading mode.
    std::optional<bool> wireframe{};          ///< Wireframe over opaque surfaces.
};

/// The visualiser kinds to change in `viewport.set_overlays` (absent: unchanged).
struct ViewportVisualisersParams
{
    std::optional<bool> lights{};     ///< Point light ranges and directional light directions.
    std::optional<bool> cameras{};    ///< Camera frusta.
    std::optional<bool> colliders{};  ///< Collider shapes.
    std::optional<bool> characters{}; ///< CharacterController capsules.
    std::optional<bool> joints{};     ///< Joint anchors, axes and limits.
    std::optional<bool> audio{};      ///< AudioSource distances and cones.
};

/// Params of `viewport.set_overlays`.
struct ViewportOverlaysParams
{
    std::optional<std::string> viewport{};                    ///< Viewport id (default "scene").
    std::optional<bool> grid{};                               ///< Ground grid.
    std::optional<bool> selectionOutline{};                   ///< Outline around selected entities.
    std::optional<bool> hover{};                              ///< Highlight of the entity under the cursor.
    std::optional<bool> icons{};                              ///< Icon billboards.
    std::optional<bool> viewCube{};                           ///< View cube and axis navigator.
    std::optional<bool> stats{};                              ///< Statistics overlay.
    std::optional<bool> cameraPreview{};                      ///< Picture-in-picture camera view.
    std::optional<ViewportVisualisersParams> visualisers{};   ///< Visualiser kinds.
    std::optional<ViewportVisualiserScope> visualiserScope{}; ///< Visualised entities.
};

/// Result of `viewport.set_mode` and `viewport.set_overlays`.
struct ViewportSettingsResult
{
    std::string viewport;      ///< Viewport id.
    ViewportSettings settings; ///< What it draws now.
};

/// Params of `viewport.project`: an entity's position or a point.
struct ViewportProjectParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    std::optional<std::string> entity{};   ///< Entity whose position to project (or point).
    std::optional<Vec3> point{};           ///< World-space point to project (or entity).
};

/// Result of `viewport.project`.
struct ViewportProjectResult
{
    f32 x = 0.0f;         ///< Pixel column (origin at the top left).
    f32 y = 0.0f;         ///< Pixel row (y down).
    f32 depth = 0.0f;     ///< Reversed-Z depth (1 at the near plane).
    bool inFront = false; ///< In front of the camera.
    bool inside = false;  ///< In front of the camera and within the image.
};

/// What `viewport.pick` does with the selection.
enum class PickSelect : u8
{
    None,    ///< Leaves it unchanged.
    Replace, ///< Selects the hit only (clears the selection on the background).
    Add,     ///< Adds the hit.
    Toggle,  ///< Adds or removes the hit.
};

/// Params of `viewport.pick`.
struct ViewportPickParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    f32 x = 0.0f;                          ///< Pixel column (origin at the top left).
    f32 y = 0.0f;                          ///< Pixel row (y down).
    std::optional<bool> cycle{}; ///< At the previous pick's pixel, return the next candidate (default false).
    std::optional<PickSelect> select{}; ///< Change the selection (default "none").
};

/// Result of `viewport.pick`.
struct ViewportPickResult
{
    std::optional<EntitySummary> entity{}; ///< The entity hit, absent for the background.
    u32 index = 0;                         ///< Its position among the candidates.
    u32 candidates = 0;                    ///< Entities under the cursor (repeated cycling picks visit each).
};

/// Result of `viewport.close`.
struct ViewportIdResult
{
    std::string viewport; ///< Viewport id.
};

/// Params of `viewport.set_aspect`.
struct ViewportAspectParams
{
    std::optional<std::string> viewport{};        ///< Viewport id (default "scene").
    ViewportAspect aspect = ViewportAspect::Free; ///< Shape of the image.
    std::optional<u32> width{};  ///< Width with "fixed", 16 to 7,680 (required there, refused otherwise).
    std::optional<u32> height{}; ///< Height with "fixed", 16 to 4,320 (required there, refused otherwise).
};

/// Params of `viewport.sample`.
struct ViewportSampleParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    f32 x = 0.0f;                          ///< Pixel column (origin at the top left).
    f32 y = 0.0f;                          ///< Pixel row (y down).
    std::optional<u32> radius{}; ///< Average the square of this many pixels around it, 0 to 8 (default 0).
};

/// Result of `viewport.sample`.
struct ViewportSampleResult
{
    Vec4 color{0.0f};      ///< Average display colour, sRGB-encoded RGBA from 0 to 1.
    f32 hue = 0.0f;        ///< Hue of the colour in degrees, 0 to 360 (0 for greys).
    f32 saturation = 0.0f; ///< HSV saturation, 0 to 1.
    f32 value = 0.0f;      ///< HSV value, 0 to 1.
    u32 pixels = 0;        ///< Pixels averaged.
};

/// Params of `viewport.snap`.
struct ViewportSnapParams
{
    std::optional<std::string> viewport{};  ///< Viewport id (default "scene").
    f32 x = 0.0f;                           ///< Pixel column (origin at the top left).
    f32 y = 0.0f;                           ///< Pixel row (y down).
    std::optional<ViewportSnapTarget> to{}; ///< "any" (default), "vertex" or "surface".
    /// Entity references left out with their descendants, e.g. the entities to be moved (default none).
    std::optional<std::vector<std::string>> exclude{};
    std::optional<f32>
        tolerance{}; ///< Largest distance of a vertex on screen in pixels, 1 to 64 (default 12).
};

/// Result of `viewport.snap`.
struct ViewportSnapResult
{
    bool hit = false;                         ///< A vertex or a surface was found.
    std::optional<ViewportSnapTarget> kind{}; ///< "vertex" or "surface".
    std::optional<Vec3> position{};           ///< Its world position.
    std::optional<Vec3> normal{};             ///< The unit surface normal (surfaces).
    std::optional<Quat> rotation{};           ///< A rotation that turns +y onto the normal (surfaces).
    std::optional<EntitySummary> entity{};    ///< The entity whose mesh or surface it is.
};

/// Params of `viewport.pick_rect`.
struct ViewportPickRectParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    f32 x = 0.0f;                          ///< Left edge in pixels (origin at the top left).
    f32 y = 0.0f;                          ///< Top edge in pixels (y down).
    f32 width = 1.0f;                      ///< Width in pixels.
    f32 height = 1.0f;                     ///< Height in pixels.
    std::optional<PickSelect> select{};    ///< Change the selection (default "none").
};

/// Result of `viewport.pick_rect`.
struct ViewportPickRectResult
{
    std::vector<EntitySummary> entities; ///< Entities drawn in the rectangle, by Entity index.
};

/// Params of `camera.bookmark_set` and `camera.bookmark_go`.
struct CameraBookmarkParams
{
    std::optional<std::string> viewport{}; ///< Viewport id (default "scene").
    std::optional<u32> slot{};             ///< Bookmark slot, 1 to 9 (default 1).
    std::optional<bool> animate{};         ///< `bookmark_go`: animate the move (default true).
};

/// Result of `render.stats`.
struct RenderStatsResult
{
    std::string viewport;      ///< Viewport id.
    u32 width = 0;             ///< Width of its image.
    u32 height = 0;            ///< Height of its image.
    ViewportRenderStats stats; ///< What the renderer did in its last frame.
};

/// Result of `render.gpu_info`.
struct RenderGpuInfoResult
{
    bool available = false;               ///< The editor renders with a GPU.
    std::optional<ViewportGpuInfo> gpu{}; ///< The GPU (absent without one).
};

/// Result of `render.diagnostics`.
struct RenderDiagnosticsResult
{
    bool available = false;   ///< The editor renders with a GPU.
    bool validation = false;  ///< The Vulkan validation layer is enabled.
    u32 validationErrors = 0; ///< Validation errors since the device was created (0 is the goal).
};

// --- clipboard ---------------------------------------------------------------------------------------------

/// Result of `clipboard.copy` and `clipboard.cut`.
struct ClipboardResult
{
    u32 entities = 0; ///< Copied entities, descendants included.
    std::string text; ///< The clipboard: scene v2 JSON of the copied entities.
};

/// Params of `clipboard.paste`.
struct ClipboardPasteParams
{
    std::optional<std::string> parent{}; ///< Parent of the pasted entities (default: roots).
    std::optional<u32> index{};          ///< Position among the siblings (default: last).
    std::optional<std::string> text{};   ///< Scene v2 JSON to paste instead of the clipboard.
};

// --- asset -------------------------------------------------------------------------------------------------

/// Params of `asset.list`.
struct AssetListParams
{
    std::optional<std::string> path{}; ///< Folder (default "assets:/").
    std::optional<bool> recursive{};   ///< Include everything below it (default false).
    std::optional<std::string> kind{}; ///< Only this kind, e.g. "Material" (see asset.info).
};

/// One asset or folder.
struct AssetEntry
{
    std::string path; ///< Virtual path.
    std::string
        kind; ///< "Folder", "Scene", "Material", "Model", "Texture", "Environment", "AudioClip" or "Other".
    u64 size = 0; ///< Size in bytes (0 for folders).
};

/// Result of `asset.list`.
struct AssetListResult
{
    std::vector<AssetEntry> assets; ///< Sorted by path.
};

/// Result of `asset.info`.
struct AssetInfoResult
{
    std::string path;     ///< Virtual path.
    std::string kind;     ///< Kind (see AssetEntry::kind).
    u64 size = 0;         ///< Size in bytes (0 for folders).
    u32 references = 0;   ///< References to it (or to anything in the folder).
    u32 dependencies = 0; ///< Assets a scene or material refers to.
};

/// Params of `asset.move`.
struct AssetMoveParams
{
    std::string from; ///< Asset or folder.
    std::string to;   ///< New path (must not exist).
};

/// Result of `asset.move`.
struct AssetMoveResult
{
    std::string from;                 ///< Old path.
    std::string to;                   ///< New path.
    u32 references = 0;               ///< References that now point to the new path.
    std::vector<std::string> updated; ///< Scene and material files rewritten, sorted (not the open scene).
};

/// Params of `asset.delete`.
struct AssetDeleteParams
{
    std::string path;            ///< Asset or folder.
    std::optional<bool> force{}; ///< Delete even if something refers to it (default false).
};

/// Result of `asset.delete`.
struct AssetDeleteResult
{
    std::string path;                       ///< The deleted path.
    std::vector<AssetReference> dangling{}; ///< References left pointing at it (only with force).
};

/// Result of `asset.references`.
struct AssetReferencesResult
{
    std::vector<AssetReference> references; ///< Sorted by source, entity, owner and field.
};

/// Result of `asset.dependencies`.
struct AssetDependenciesResult
{
    std::vector<std::string> dependencies; ///< Referenced assets, sorted, each once.
};

// --- material ----------------------------------------------------------------------------------------------

/// Params of `material.create`.
struct MaterialCreateParams
{
    std::string path;                ///< New `.material` file.
    std::optional<JsonText> value{}; ///< Fields: {"material": {...}, "textures": {...}} (default: defaults).
    std::optional<bool> overwrite{}; ///< Replace an existing file (default false).
};

/// Result of `material.get`.
struct MaterialGetResult
{
    std::string path; ///< The material file.
    JsonText value;   ///< Every field: {"material": {...}, "textures": {...}}.
};

/// Params of `material.set`.
struct MaterialSetParams
{
    std::string path; ///< The material file.
    JsonText value;   ///< Merge patch of fields, e.g. {"material": {"roughnessFactor": 0.2}}.
};

/// Result of `material.set`.
struct MaterialSetResult
{
    std::string path;     ///< The material file.
    bool changed = false; ///< False if it was already like that.
};

// --- log ---------------------------------------------------------------------------------------------------

/// Params of `log.read`.
struct LogReadParams
{
    std::optional<u64> since{}; ///< Only messages after this sequence (`last` of an earlier read).
    std::optional<std::string>
        minLevel{}; ///< Lowest level: "trace", "debug", "info", "warn", "error", "fatal".
    std::optional<std::string> category{}; ///< Only this category, e.g. "Editor".
    std::optional<std::string> contains{}; ///< Only messages whose text contains this.
    std::optional<u32> limit{};            ///< Most messages to return (default 100, at most 1000); without
                                           ///< since, the newest.
};

/// One log message.
struct LogEntry
{
    u64 sequence = 0;     ///< Position in the log, increasing.
    f64 time = 0.0;       ///< Seconds since the logger started.
    std::string level;    ///< Level, e.g. "warn".
    std::string category; ///< Category.
    std::string text;     ///< Message.
};

/// Result of `log.read`.
struct LogReadResult
{
    std::vector<LogEntry> entries; ///< Oldest first.
    u64 last = 0;                  ///< Sequence to pass as since to read on (after the last message read).
    u64 dropped = 0;               ///< Messages lost because the ring was full.
};

/// Params of `log.set_level`.
struct LogSetLevelParams
{
    std::string level; ///< Lowest level written: "trace", "debug", "info", "warn", "error", "fatal" or "off".
};

/// Result of `log.set_level` and `log.clear`.
struct LogStateResult
{
    std::string level; ///< Lowest level written.
    u64 last = 0;      ///< Sequence of the newest message.
};

// --- ui ----------------------------------------------------------------------------------------------------

/// A panel or window in `ui.state`.
struct UiPanelEntry
{
    std::string id;      ///< Stable id, e.g. "panel.console".
    std::string title;   ///< Localized title.
    bool open = false;   ///< Shown.
    bool docked = false; ///< Part of the dock layout.
};

/// A layout in `ui.state`.
struct UiLayoutEntry
{
    std::string name;     ///< Name, e.g. "default".
    bool builtIn = false; ///< Built in (cannot be deleted).
    bool active = false;  ///< Applied last.
};

/// Result of `ui.state`.
struct UiStateResult
{
    std::vector<UiPanelEntry> panels;   ///< Every panel and window.
    std::vector<UiLayoutEntry> layouts; ///< Built-in layouts, then saved ones.
};

/// Params of `ui.show_panel`.
struct UiShowPanelParams
{
    std::string panel;          ///< Panel or window id, e.g. "panel.console".
    std::optional<bool> open{}; ///< Show and focus it (default true) or hide it.
};

/// Params of `ui.layout_apply`, `ui.layout_save` and `ui.layout_delete`.
struct UiLayoutParams
{
    std::string name; ///< Layout name: letters, digits, '-' and '_', at most 64 bytes.
};

/// What the command palette lists.
enum class PaletteMode : u8
{
    Commands, ///< Commands and actions.
    Entities, ///< Entities of the scene ("Go to Anything").
    Settings, ///< Preference pages.
};

/// Params of `ui.palette`.
struct UiPaletteParams
{
    std::optional<PaletteMode> mode{};  ///< What to list (default "commands").
    std::optional<std::string> query{}; ///< Text typed into the search field.
};

/// Params of `ui.notify`.
struct UiNotifyParams
{
    std::string message;             ///< Text of the toast (at most 256 bytes).
    std::optional<UiNotice> level{}; ///< Severity (default "info").
};

/// Params of `editor.screenshot`.
struct ScreenshotParams
{
    std::optional<u32> width{};  ///< Width in pixels, 64 to 7,680 (headless only; default: --ui-size).
    std::optional<u32> height{}; ///< Height in pixels, 64 to 4,320 (headless only; default: --ui-size).
};

/// An image in a result.
struct ResultImage
{
    std::string mimeType; ///< "image/png".
    std::string data;     ///< The file, base64-encoded.
};

/// Result of `editor.screenshot`.
struct ScreenshotResult
{
    u32 width = 0;                    ///< Width in pixels.
    u32 height = 0;                   ///< Height in pixels.
    std::vector<ResultImage> _images; ///< The PNG (MCP clients receive it as image content).
};

// --- preferences -------------------------------------------------------------------------------------------

/// Params of `preferences.set`.
struct PreferencesSetParams
{
    /// Object of preference values: a merge patch of the current preferences (see `preferences.get`).
    JsonText value;
    std::optional<bool> replace{}; ///< Reset the fields the patch does not mention (default false).
};

/// Result of `preferences.get` and `preferences.set`.
struct PreferencesResult
{
    JsonText value; ///< Every preference.
};

// --- play --------------------------------------------------------------------------------------------------

/// Params of `play.start`.
struct PlayStartParams
{
    std::optional<std::string> mode{}; ///< "play" (default: the whole game) or "simulate" (physics only).
    std::optional<bool> fixedStep{};   ///< One physics step per frame and seeded ids (default false).
    std::optional<u64> seed{};         ///< Seed of the ids of runtime spawns with fixedStep (default 0).
};

/// Result of the `play.*` commands: the state of the play session.
struct PlayStatusResult
{
    std::string state;      ///< "stopped", "playing" or "paused".
    std::string mode;       ///< "play" or "simulate".
    bool fixedStep = false; ///< Deterministic steps.
    f64 timeScale = 1.0;    ///< Multiplier of the frame time.
    u32 steps = 0;          ///< Updates of the play World so far (saturates).
    f64 time = 0.0;         ///< Game time so far, in seconds.
};

/// Params of `play.pause`.
struct PlayPauseParams
{
    bool paused = true; ///< Pause (true) or resume (false).
};

/// Params of `play.step`.
struct PlayStepParams
{
    std::optional<u32> steps{}; ///< Fixed physics steps to advance at once (default 1).
};

/// Params of `play.set_time_scale`.
struct PlayTimeScaleParams
{
    f64 scale = 1.0; ///< Multiplier of the frame time: 0 freezes, 0.5 is slow motion.
};

/// Params of `play.run_until`.
struct PlayRunUntilParams
{
    u32 maxSteps = 0;   ///< Steps to give up after.
    std::string entity; ///< Entity reference in the play World.
    /// Stop once the entity exists (true) or is gone (false). Without it, compare a value instead.
    std::optional<bool> exists{};
    std::optional<std::string> component{}; ///< Stable name of the Component to compare.
    std::optional<std::string> field{}; ///< JSON Pointer into its value, e.g. "/lines" ("" or absent: all).
    std::optional<JsonText> equals{};   ///< The value to wait for.
};

/// Result of `play.run_until`.
struct PlayRunUntilResult
{
    bool met = false;        ///< The condition held before maxSteps.
    u32 steps = 0;           ///< Steps taken by this call.
    PlayStatusResult status; ///< The session afterwards.
};

/// Params of `play.keep_changes`.
struct PlayKeepChangesParams
{
    std::vector<std::string> entities;                    ///< Entity references in the play World.
    std::optional<std::vector<std::string>> components{}; ///< Stable Component names (default: all saved).
};

/// Result of `play.keep_changes`.
struct PlayKeepChangesResult
{
    u32 entities = 0;   ///< Entities whose values were copied.
    u32 components = 0; ///< Component values copied in all.
};

// --- module ------------------------------------------------------------------------------------------------

/// Params of `module.load`.
struct ModuleLoadParams
{
    std::string path; ///< File path of the module's shared library.
};

/// Params of `module.unload`.
struct ModuleUnloadParams
{
    std::string name; ///< The module.
};

/// Result of the `module.*` commands.
struct ModuleListResult
{
    std::vector<PlayModuleInfo> modules; ///< Loaded modules in load order.
};

/// Params of `module.create`.
struct ModuleCreateParams
{
    std::string name{}; ///< The module: lower case letters, digits and underscores, starting with a letter.
};

/// Result of `module.create`.
struct ModuleCreateResult
{
    std::string directory{};          ///< The module's directory, e.g. "modules:/tetris".
    std::vector<std::string> files{}; ///< Files written, as virtual paths.
};

/// Params of `module.build`.
struct ModuleBuildParams
{
    /// Modules to build, by directory name under `modules:/` (default: all).
    std::optional<std::vector<std::string>> modules{};
    std::optional<bool> reconfigure{}; ///< Run the CMake configure step again (default false).
    std::optional<bool> load{};        ///< Load or hot-reload the built modules (default true).
};

/// One compiler, linker or CMake diagnostic of `module.build`.
struct ModuleDiagnostic
{
    /// The file: a virtual path such as "modules:/tetris/tetris.cpp" inside the project, otherwise as the
    /// tool printed it ("" when it names none).
    std::string file{};
    u32 line = 0;           ///< 1-based line (0: none).
    u32 column = 0;         ///< 1-based column (0: none).
    std::string severity{}; ///< "error", "warning" or "note".
    std::string code{};     ///< Tool code, e.g. "C2065" or "-Wunused-variable" ("" for none).
    std::string message{};  ///< What the tool said.
};

/// Result of `module.build`.
struct ModuleBuildResult
{
    bool succeeded = false;                      ///< Every step succeeded.
    bool configured = false;                     ///< The build ran a CMake configure step.
    u32 errors = 0;                              ///< Diagnostics with severity "error".
    u32 warnings = 0;                            ///< Diagnostics with severity "warning".
    std::vector<ModuleDiagnostic> diagnostics{}; ///< Diagnostics in output order (at most 200).
    std::vector<std::string> libraries{};        ///< File paths of the built module libraries.
    std::vector<std::string> loaded{};           ///< Modules loaded for the first time.
    std::vector<std::string> reloaded{};         ///< Modules hot-reloaded.
    std::vector<PlayModuleInfo> modules{};       ///< Loaded modules in load order.
    std::string output{};                        ///< The last lines of CMake's output.
};

/// Params of `editor.open_source`.
struct OpenSourceParams
{
    /// The file: a virtual path such as "modules:/tetris/tetris.cpp", or an absolute file path.
    std::string path{};
    std::optional<u32> line{};   ///< 1-based line to show (default 1).
    std::optional<u32> column{}; ///< 1-based column to show (default 1).
};

/// Result of `editor.open_source`.
struct OpenSourceResult
{
    std::string program{};                ///< The program that was started.
    std::vector<std::string> arguments{}; ///< Its arguments.
};

/// Params of `module.reload`.
struct ModuleReloadParams
{
    std::optional<std::string>
        name{}; ///< The module to reload (default: every module whose library changed).
};

/// Result of `module.reload`.
struct ModuleReloadResult
{
    std::vector<std::string> reloaded{};   ///< Modules reloaded.
    std::vector<PlayModuleInfo> modules{}; ///< Loaded modules in load order.
};

// --- input -------------------------------------------------------------------------------------------------

/// Params of `input.actions`.
struct InputAssetParams
{
    std::string asset; ///< The input actions asset, e.g. "assets:/input/game.input".
};

/// One action of an input actions asset.
struct InputActionInfo
{
    std::string name;                  ///< The action.
    std::string type;                  ///< "Button", "Axis" or "Vector2".
    std::vector<std::string> bindings; ///< Bindings in text form, e.g. "Key.Space".
};

/// Result of the `input.*` asset commands.
struct InputActionsResult
{
    std::string asset;                    ///< The asset.
    std::vector<InputActionInfo> actions; ///< Its actions in order.
};

/// Params of `input.action_define`.
struct InputActionDefineParams
{
    std::string asset;                                  ///< The asset (created when missing).
    std::string name;                                   ///< The action; an existing one is replaced.
    std::string type;                                   ///< "Button", "Axis" or "Vector2".
    std::optional<std::vector<std::string>> bindings{}; ///< Its bindings (default none).
};

/// Params of `input.action_remove`.
struct InputActionParams
{
    std::string asset; ///< The asset.
    std::string name;  ///< The action.
};

/// Params of `input.action_rename`.
struct InputActionRenameParams
{
    std::string asset;   ///< The asset.
    std::string name;    ///< The action.
    std::string newName; ///< Its new name.
};

/// Params of `input.bind` and `input.unbind`.
struct InputBindParams
{
    std::string asset;   ///< The asset.
    std::string action;  ///< The action.
    std::string binding; ///< The binding in text form, e.g. "Key.Space" or "Gamepad.South".
};

/// A key change for `input.inject`.
struct InputKeyEvent
{
    std::string key;  ///< Key name, e.g. "Space", "Left", "A".
    bool down = true; ///< Pressed (true) or released.
};

/// A mouse button change for `input.inject`.
struct InputButtonEvent
{
    std::string button; ///< "Left", "Right" or "Middle".
    bool down = true;   ///< Pressed (true) or released.
};

/// Params of `input.inject`.
struct InputInjectParams
{
    std::optional<std::vector<InputKeyEvent>> keys{};            ///< Key changes, in order.
    std::optional<std::vector<InputButtonEvent>> mouseButtons{}; ///< Mouse button changes, in order.
    std::optional<Vec2> mousePosition{};                         ///< Cursor position in the game view.
};

/// Params of `input.read`.
struct InputReadParams
{
    std::optional<std::vector<std::string>> actions{}; ///< Only these actions (default: all).
};

/// Result of `input.read`.
struct InputReadResult
{
    std::vector<PlayActionState> actions; ///< The actions of the running game.
};

} // namespace illusion::editor

/// Names of TransformSpace values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::TransformSpace>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"world", 0}, illusion::EnumValue{"local", 1},
                                        illusion::EnumValue{"self", 2}};
};

/// Names of TransformPivot values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::TransformPivot>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"individual", 0},
                                        illusion::EnumValue{"center", 1}};
};

/// Names of TransformAxis values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::TransformAxis>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"x", 0}, illusion::EnumValue{"y", 1},
                                        illusion::EnumValue{"z", 2}};
};

/// Names of AlignEdge values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::AlignEdge>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"min", 0}, illusion::EnumValue{"center", 1},
                                        illusion::EnumValue{"max", 2}};
};

/// Names of AlignTarget values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::AlignTarget>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"selection", 0},
                                        illusion::EnumValue{"active", 1}};
};

/// Names of DistributeMode values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::DistributeMode>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"centers", 0}, illusion::EnumValue{"gaps", 1}};
};

/// Names of DropTarget values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::DropTarget>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"surfaces", 0}, illusion::EnumValue{"plane", 1}};
};

/// Names of GizmoModalOperation values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GizmoModalOperation>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"translate", 0}, illusion::EnumValue{"rotate", 1},
                                        illusion::EnumValue{"scale", 2}};
};

/// Names of GizmoTool values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GizmoTool>
{
    /// The values.
    static constexpr std::array kValues{
        illusion::EnumValue{"select", 0},    illusion::EnumValue{"translate", 1},
        illusion::EnumValue{"rotate", 2},    illusion::EnumValue{"scale", 3},
        illusion::EnumValue{"universal", 4}, illusion::EnumValue{"measure", 5}};
};

/// Names of ViewportSnapTarget values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportSnapTarget>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"any", 0}, illusion::EnumValue{"vertex", 1},
                                        illusion::EnumValue{"surface", 2}};
};

/// Names of GizmoSpace values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GizmoSpace>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"world", 0}, illusion::EnumValue{"local", 1},
                                        illusion::EnumValue{"view", 2}};
};

/// Names of GizmoPivot values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GizmoPivot>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"median", 0}, illusion::EnumValue{"active", 1},
                                        illusion::EnumValue{"individual", 2},
                                        illusion::EnumValue{"bounds_center", 3}};
};

/// Names of PaletteMode values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::PaletteMode>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"commands", 0},
                                        illusion::EnumValue{"entities", 1},
                                        illusion::EnumValue{"settings", 2}};
};

/// Names of UiNotice values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::UiNotice>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"info", 0}, illusion::EnumValue{"success", 1},
                                        illusion::EnumValue{"warning", 2}, illusion::EnumValue{"error", 3}};
};

/// Names of ViewportProjection values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportProjection>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"perspective", 0},
                                        illusion::EnumValue{"orthographic", 1}};
};

/// Names of ViewportShading values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportShading>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"lit", 0},
                                        illusion::EnumValue{"unlit", 1},
                                        illusion::EnumValue{"albedo", 2},
                                        illusion::EnumValue{"normals", 3},
                                        illusion::EnumValue{"roughness", 4},
                                        illusion::EnumValue{"metallic", 5},
                                        illusion::EnumValue{"ambientOcclusion", 6},
                                        illusion::EnumValue{"shadowCascades", 7},
                                        illusion::EnumValue{"motionVectors", 8}};
};

/// Names of ViewportView values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportView>
{
    /// The values.
    static constexpr std::array kValues{
        illusion::EnumValue{"perspective", 0}, illusion::EnumValue{"top", 1},
        illusion::EnumValue{"bottom", 2},      illusion::EnumValue{"front", 3},
        illusion::EnumValue{"back", 4},        illusion::EnumValue{"left", 5},
        illusion::EnumValue{"right", 6}};
};

/// Names of PickSelect values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::PickSelect>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"none", 0}, illusion::EnumValue{"replace", 1},
                                        illusion::EnumValue{"add", 2}, illusion::EnumValue{"toggle", 3}};
};

/// Names of ViewportVisualiserScope values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportVisualiserScope>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"selected", 0}, illusion::EnumValue{"all", 1}};
};

/// Names of ViewportKind values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportKind>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"scene", 0}, illusion::EnumValue{"game", 1}};
};

/// Names of ViewportAspect values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::ViewportAspect>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"free", 0},  illusion::EnumValue{"16:9", 1},
                                        illusion::EnumValue{"16:10", 2}, illusion::EnumValue{"4:3", 3},
                                        illusion::EnumValue{"21:9", 4},  illusion::EnumValue{"1:1", 5},
                                        illusion::EnumValue{"fixed", 6}};
};
