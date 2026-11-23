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
#include <illusion/editor/core/Export.hpp>
#include <illusion/editor/core/GamepadHost.hpp>
#include <illusion/editor/core/Gizmo.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/Snap.hpp>
#include <illusion/editor/core/UiHost.hpp>
#include <illusion/editor/core/UiPlacementEdit.hpp>
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
    std::optional<DropTarget> onto{}; ///< "surfaces" (default), "plane" or "colliders".
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
    std::optional<bool> perception{}; ///< Sight cones and what agents see and remember (play World).
    std::optional<bool> skeleton{};   ///< Joints and bones of Animators, IK targets, springs and ragdolls.
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
    /// "Folder", "Scene", "Prefab", "Material", "Model", "Texture", "Environment", "AudioClip", "Font",
    /// "AnimationGraph", "RagdollProfile", "BehaviorTree", "Shader" or "Other".
    std::string kind;
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
    /// Every import setting of a texture or model, defaults included, e.g. {"texture": {"maxSize": 0, ...}};
    /// absent for other assets and when its sidecar is invalid.
    std::optional<JsonText> importSettings{};
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

/// Params of `asset.import`.
struct AssetImportParams
{
    /// Absolute paths of the external files to copy into the project (OS file drops are absolute paths).
    std::vector<std::string> files;
    std::optional<std::string> destination{}; ///< Target folder (default "assets:/").
    std::optional<bool> overwrite{};          ///< Replace a file of the same name (default false).
};

/// Result of `asset.import`.
struct AssetImportResult
{
    std::vector<std::string> imported; ///< The new virtual paths, in the order they were imported.
};

/// Params of `asset.relink`.
struct AssetRelinkParams
{
    std::string from; ///< The old (usually missing) path the references point to.
    std::string to;   ///< The new path the references should point to.
};

/// Result of `asset.relink`.
struct AssetRelinkResult
{
    std::string from;                 ///< Old path.
    std::string to;                   ///< New path.
    u32 references = 0;               ///< References that now point to the new path.
    std::vector<std::string> updated; ///< Scene and material files rewritten, sorted (not the open scene).
};

/// Result of `asset.cook`.
struct AssetCookResult
{
    u32 images = 0;                  ///< Images cooked.
    u32 models = 0;                  ///< glTF models cooked (with the images they use).
    u32 materials = 0;               ///< Material files checked.
    u32 fonts = 0;                   ///< Fonts cooked (.ttf, .otf).
    std::vector<std::string> errors; ///< One message per file that failed.
};

/// Params of `asset.pack`.
struct AssetPackParams
{
    std::string
        path; ///< Target `.illpak` (a project path such as "settings:/game.illpak", or an absolute path).
};

/// Result of `asset.pack`.
struct AssetPackResult
{
    std::string path; ///< The `.illpak` that was written.
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

/// Result of `asset.find_missing`.
struct AssetFindMissingResult
{
    /// References to `assets:` paths that do not exist, sorted by source, entity, owner and field: in scenes,
    /// materials, settings, the open scene, the manifest and the external files of `.gltf` models.
    std::vector<AssetReference> references{};
    std::vector<std::string> assets{}; ///< The missing paths, without `#` fragments, sorted, each once.
};

/// Params of `asset.set_import_settings`.
struct AssetSetImportSettingsParams
{
    std::string path; ///< The texture or model.
    /// Merge patch of settings, e.g. {"texture": {"maxSize": 1024}}; null resets a setting, a section or (as
    /// the whole value) everything to the defaults.
    JsonText value;
};

/// Result of `asset.set_import_settings`.
struct AssetImportSettingsResult
{
    std::string path;     ///< The texture or model.
    std::string sidecar;  ///< Its sidecar, or "" if every setting is a default (no sidecar exists).
    JsonText value;       ///< Every setting afterwards, defaults included.
    bool changed = false; ///< True if the sidecar was written or deleted.
};

/// Result of `asset.reimport`.
struct AssetReimportResult
{
    std::vector<std::string> assets; ///< The textures and models asked to import again, sorted.
};

/// Params of `asset.thumbnail`.
struct AssetThumbnailParams
{
    std::string path;              ///< A model, material file, texture or `.hdr` environment in `assets:`.
    std::optional<bool> refresh{}; ///< Make it again even if the cache has it (default false).
};

/// Result of `asset.thumbnail`.
struct AssetThumbnailResult
{
    std::string path;      ///< The asset.
    std::string kind;      ///< "Model", "Material", "Texture" or "Environment".
    std::string thumbnail; ///< Virtual path of the PNG, e.g. "thumbnails:/0123456789abcdef.png".
    std::string file;      ///< Absolute path of the PNG (UTF-8).
    u32 width = 0;         ///< Width in pixels.
    u32 height = 0;        ///< Height in pixels.
    bool cached = false;   ///< The PNG was in the cache already (nothing was rendered).
};

// --- prefab ------------------------------------------------------------------------------------------------

/// Params of `prefab.create`.
struct PrefabCreateParams
{
    std::string entity;              ///< Root of the entities to save (with its descendants).
    std::string path;                ///< New `.prefab` file in the assets: mount.
    std::optional<bool> overwrite{}; ///< Replace an existing file (default false).
    std::optional<bool>
        replace{}; ///< Replace the entities with an instance of the new prefab (default true).
};

/// Result of `prefab.create`.
struct PrefabCreateResult
{
    std::string path;                        ///< The prefab file.
    bool created = false;                    ///< True if the file did not exist.
    u32 entities = 0;                        ///< Entities saved in the prefab.
    std::optional<EntitySummary> instance{}; ///< Root of the instance that replaced them (with replace).
};

/// Params of `prefab.instantiate`.
struct PrefabInstantiateParams
{
    std::string path;                    ///< The `.prefab` file.
    std::optional<std::string> parent{}; ///< Parent entity (default: the instance is a root).
    std::optional<u32> index{};          ///< Position among the parent's children (default: last).
    std::optional<Vec3> position{};      ///< World position of the root (default: as in the prefab).
};

/// An instance of a prefab: result of `prefab.instantiate`.
struct PrefabInstanceResult
{
    EntitySummary root;   ///< The instance's root.
    std::string instance; ///< Id of the instance (16 hex digits), from which its entity ids derive.
    std::string prefab;   ///< The prefab file.
    u32 entities = 0;     ///< Entities of the instance.
};

/// Params of `prefab.unpack` and `prefab.overrides`.
struct PrefabEntityParams
{
    std::string entity; ///< Any entity of the instance.
};

/// Result of `prefab.unpack`.
struct PrefabUnpackResult
{
    u32 entities = 0; ///< Entities that are no longer linked to a prefab.
};

/// Result of `prefab.overrides`.
struct PrefabOverridesResult
{
    EntitySummary root;   ///< The instance's root.
    std::string instance; ///< Id of the instance (16 hex digits).
    std::string prefab;   ///< The prefab file.
    /// What the scene changes, as in the instance's record: by id in the prefab file, Component patches
    /// (null: the Component is removed) or null (the entity is removed). {} when nothing is overridden.
    JsonText overrides;
};

/// Params of `prefab.apply`.
struct PrefabApplyParams
{
    std::string entity;                     ///< An entity of an instance.
    std::optional<std::string> component{}; ///< Only this Component (default: every Component of the entity).
    std::optional<std::string> field{};     ///< Only this field of the Component (needs component).
    std::optional<bool> all{}; ///< The whole instance, removed entities included (default false).
};

/// Result of `prefab.apply`.
struct PrefabApplyResult
{
    std::string path;   ///< The prefab file that was written (unchanged when components is 0).
    u32 components = 0; ///< Components (and removed entities) written into the prefab.
    u32 instances = 0;  ///< Other instances in the open scene that changed to follow the prefab.
};

/// Params of `prefab.refresh`.
struct PrefabRefreshParams
{
    std::optional<std::string> path{}; ///< Only this prefab file (default: every changed one).
};

/// Result of `prefab.refresh`.
struct PrefabRefreshResult
{
    std::vector<std::string> prefabs; ///< Prefab files that had changed.
    u32 instances = 0;                ///< Instances of the open scene that changed to follow them.
};

/// Params of `prefab.open`.
struct PrefabOpenParams
{
    std::string path;                     ///< The `.prefab` file to edit.
    std::optional<bool> discardChanges{}; ///< Close a scene with unsaved changes (default false).
};

/// Params of `prefab.close`.
struct PrefabCloseParams
{
    std::optional<bool> discardChanges{}; ///< Leave a prefab with unsaved changes (default false).
};

/// Params of `prefab.revert`.
struct PrefabRevertParams
{
    std::string entity;                     ///< An entity of an instance.
    std::optional<std::string> component{}; ///< Only this Component (default: every Component but Hierarchy).
    std::optional<std::string> field{};     ///< Only this field of the Component (needs component).
};

/// Result of `prefab.revert`.
struct PrefabRevertResult
{
    u32 components = 0; ///< Components given back the prefab's value (added or removed included).
};

// --- material ----------------------------------------------------------------------------------------------

/// Params of `material.create`.
struct MaterialCreateParams
{
    std::string path;                ///< New `.material` file.
    std::optional<JsonText> value{}; ///< Fields: {"material": {...}, "textures": {...}} (default: defaults).
    std::optional<bool> overwrite{}; ///< Replace an existing file (default false).
};

/// A message of the shader compiler (`material.compile`, `material.validate`, `material.get`).
struct ShaderDiagnosticInfo
{
    std::string file; ///< The file the message is about, e.g. "assets:/shaders/glow.shader" ("" for none).
    u32 line = 0;     ///< 1-based line (0: none).
    u32 column = 0;   ///< 1-based column (0: none).
    std::string severity; ///< "error", "warning" or "note".
    std::string code;     ///< The compiler's code, e.g. "E30015" ("" for none).
    std::string message;  ///< What is wrong.
};

/// A parameter or texture that a material shader declares: a name that "parameters" of a `.material` takes.
struct MaterialParameterInfo
{
    std::string name; ///< The field of the shader's Parameters.
    /// "float", "float2", "float3", "float4", "int", "bool" or "texture".
    std::string type;
    bool color = false;           ///< A colour (float3 or float4 with `[Color]`).
    std::optional<f64> minimum{}; ///< Lowest value the editor offers (`[Range]`).
    std::optional<f64> maximum{}; ///< Highest value the editor offers (`[Range]`).
    /// The default, as the file writes it: a number, `true`/`false`, an array of numbers, or the path of the
    /// default image of a texture ("" for the fallback).
    JsonText defaultValue;
    std::string usage; ///< Textures: "color", "linear", "normal" or "mask" ("" for the others).
};

/// The build state of a material shader and what it declares.
struct MaterialShaderInfo
{
    std::string path; ///< The `.shader` file, or a `builtin:/shader/...` path.
    /// "ok" (built from the file as it is now), "failed" (the file does not compile), "building" (queued or
    /// compiling on the background thread), "missing" (no such file) or "builtin".
    std::string status;
    std::string shadingModel;                      ///< "Default", "Unlit", ... ("" until it built).
    bool worldOffset = false;                      ///< The shader moves vertices.
    std::vector<MaterialParameterInfo> parameters; ///< Parameters, then textures, in field order.
    std::vector<ShaderDiagnosticInfo> diagnostics; ///< Errors, warnings and notes of the last build.
    std::string error;                             ///< Why there is no layout ("" when there is one).
};

/// Result of `material.get`.
struct MaterialGetResult
{
    std::string path; ///< The material file.
    /// Every field: {"shader": "...", "material": {...}, "textures": {...}, "parameters": {...}}.
    JsonText value;
    /// The shader the material names with what it declares (absent for a material of the standard shader).
    std::optional<MaterialShaderInfo> shader{};
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

/// Params of `material.compile`.
struct MaterialCompileParams
{
    std::string path; ///< A `.shader` file of the assets: mount.
};

/// Params of `material.validate`.
struct MaterialValidateParams
{
    std::string path; ///< A `.material` or `.shader` file of the assets: mount.
};

/// Result of `material.validate`.
struct MaterialValidateResult
{
    std::string path;      ///< The file.
    std::string kind;      ///< "Material" or "Shader".
    bool valid = false;    ///< A material that parses and fits its shader, or a shader that built.
    std::string error;     ///< The first problem ("" if valid or still building).
    std::string errorPath; ///< Its JSON Pointer, e.g. "/parameters/glow" ("" if none).
    /// The shader of the file (for a material, the one it names): its build state and what it declares.
    std::optional<MaterialShaderInfo> shader{};
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

// --- profiler and diagnostics ------------------------------------------------------------------------------

/// Params of `profiler.get`.
struct ProfilerGetParams
{
    std::optional<u32> frames{}; ///< Newest frames to report (default 120, at most the frames kept).
};

/// Time spent in one place of the editor's frame over the reported frames.
struct ProfilerScopeEntry
{
    std::string name;     ///< Name of the scope, e.g. "ui" or "panel.outliner".
    u32 depth = 0;        ///< Nesting depth: 0 for the frame's phases.
    u32 calls = 0;        ///< Times it ran in the newest frame.
    f32 lastMs = 0.0f;    ///< Milliseconds in the newest frame.
    f32 averageMs = 0.0f; ///< Milliseconds per frame, averaged over the reported frames.
    f32 maxMs = 0.0f;     ///< Most milliseconds in one frame.
};

/// Result of `profiler.get`.
struct ProfilerGetResult
{
    bool paused = false;     ///< Recording is paused (the frames are kept).
    u32 frameIndex = 0;      ///< Frames the editor has run (saturates at 4,294,967,295).
    std::vector<f32> frames; ///< Milliseconds per reported frame, oldest first.
    f32 averageMs = 0.0f;    ///< Average of frames (0 without frames).
    f32 maxMs = 0.0f;        ///< Largest of frames.
    f32 fps = 0.0f;          ///< 1000 / averageMs (0 without frames).
    bool truncated = false;  ///< A reported frame had more scopes than the profiler keeps.
    /// Places, in the newest frame's order (children follow their parents).
    std::vector<ProfilerScopeEntry> scopes;
};

/// Params of `profiler.set`.
struct ProfilerSetParams
{
    std::optional<bool> paused{}; ///< Pause or resume recording.
    std::optional<bool> clear{};  ///< Forget the recorded frames (default false).
};

/// Result of `profiler.set`.
struct ProfilerSetResult
{
    bool paused = false; ///< Recording is paused.
    u32 frames = 0;      ///< Frames recorded now.
};

/// One problem found by `diagnostics.get`.
struct DiagnosticEntry
{
    std::string severity; ///< "error" or "warning".
    /// What kind of problem: "render.validation", "render.lights_dropped", "log.errors", "log.warnings",
    /// "asset.missing" or "prefab.unresolved".
    std::string code;
    /// What it is about: a viewport id, an asset path or an entity id ("" for the editor as a whole).
    std::string subject;
    u32 count = 0;       ///< How many: validation errors, messages, references or lights.
    std::string message; ///< The problem in English.
};

/// Result of `diagnostics.get`.
struct DiagnosticsResult
{
    u32 errors = 0;                      ///< Issues with severity "error".
    u32 warnings = 0;                    ///< Issues with severity "warning".
    std::vector<DiagnosticEntry> issues; ///< Errors first, then warnings, each in a stable order.
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
    Assets,   ///< Assets and folders of the project ("#").
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

/// One bus of a play session's mixer.
struct PlayBusEntry
{
    std::string name;    ///< "sfx", "music", "ambience", "dialogue" or "ui".
    f32 volume = 1.0f;   ///< Linear gain.
    bool paused = false; ///< Paused: its sounds keep their place but are silent.
};

/// Result of `play.diagnostics` and `play.set_mix`: the physics and audio of the running session.
struct PlayDiagnosticsResult
{
    bool running = false;            ///< A play session runs (everything else is zero otherwise).
    u32 bodies = 0;                  ///< Physics bodies (characters count one each).
    u32 characters = 0;              ///< Character controllers.
    u32 joints = 0;                  ///< Joints.
    u32 physicsSteps = 0;            ///< Fixed physics steps so far (saturates at 4,294,967,295).
    u32 contacts = 0;                ///< Contact events of the last step.
    u32 activeVoices = 0;            ///< Sounds playing (or paused on a bus).
    u32 droppedSounds = 0;           ///< Sounds that could not start: every voice had a higher priority.
    u32 stolenVoices = 0;            ///< Voices taken from lower-priority sounds.
    u32 clips = 0;                   ///< Loaded audio clips.
    u32 loadingClips = 0;            ///< Clips loading in the background.
    u32 clipKilobytes = 0;           ///< Memory held by loaded clips, in KiB (rounded up).
    f32 masterVolume = 1.0f;         ///< Linear gain of the whole mix.
    std::vector<PlayBusEntry> buses; ///< The buses, in AudioBus order.
};

/// Params of `play.set_mix`.
struct PlaySetMixParams
{
    std::optional<f32> masterVolume{}; ///< Linear gain of the whole mix, 0 to 4.
    std::optional<std::string> bus{};  ///< The bus that `volume` and `paused` change.
    std::optional<f32> volume{};       ///< Linear gain of the bus, 0 to 4.
    std::optional<bool> paused{};      ///< Pause or resume the bus.
};

/// Params of `particles.play`, `particles.stop` and `particles.clear`.
struct ParticlesParams
{
    std::string entity; ///< Entity reference of an emitter.
};

/// Params of `particles.emit`.
struct ParticlesEmitParams
{
    std::string entity; ///< Entity reference of an emitter.
    u32 count = 1;      ///< Particles to emit now (1 to 65536).
};

/// Result of `particles.play`, `particles.stop`, `particles.clear` and `particles.emit`.
struct ParticlesResult
{
    u32 alive = 0;        ///< Live particles of the emitter.
    bool playing = false; ///< The emitter emits.
    u32 emitted = 0;      ///< `particles.emit`: particles that fit below its maxParticles.
};

/// Params of `particles.stats`.
struct ParticlesStatsParams
{
    std::optional<std::string> entity{}; ///< One emitter (default: every emitter of the World).
};

/// Result of `particles.stats`.
struct ParticlesStatsResult
{
    u32 emitters = 0; ///< Emitters counted.
    u32 playing = 0;  ///< Of them, those that emit.
    u32 alive = 0;    ///< Their live particles.
};

/// Params of `nav.bake`.
struct NavBakeParams
{
    std::optional<std::string> surface{}; ///< Entity reference of a NavMeshSurface (default: every surface).
    /// The `.navmesh` file of `surface` when its `data` is empty, e.g. "assets:/navigation/yard.navmesh"
    /// (default: `assets:/navigation/<entity name>.navmesh`).
    std::optional<std::string> path{};
    std::optional<bool> force{}; ///< Build every tile again instead of the stale ones (default false).
};

/// What `nav.bake` did for one surface.
struct NavBakeSurface
{
    EntitySummary entity;     ///< The NavMeshSurface's Entity.
    std::string path;         ///< The `.navmesh` file.
    u32 tiles = 0;            ///< Tiles of the grid.
    u32 rebuiltTiles = 0;     ///< Tiles built again (all of them for a first bake).
    u32 bytes = 0;            ///< Size of the file.
    bool written = false;     ///< The file changed and was written.
    bool dataChanged = false; ///< `NavMeshSurface::data` was set to the file.
};

/// Result of `nav.bake`.
struct NavBakeResult
{
    std::vector<NavBakeSurface> surfaces; ///< Every surface that was baked, in Entity order.
    u32 rebuiltTiles = 0;                 ///< Tiles built again in all.
    u32 written = 0;                      ///< Files written.
};

/// Params of `nav.status`.
struct NavStatusParams
{
    std::optional<bool> wait{}; ///< Finish loading and baking first (default true).
};

/// A NavMeshSurface in the result of `nav.status`.
struct NavStatusSurface
{
    EntitySummary entity; ///< The NavMeshSurface's Entity.
    u32 agentType = 0;    ///< Agent type it serves.
    std::string data;     ///< Its `.navmesh` file ("" for none).
    bool ready = false;   ///< Its navmesh has every tile.
};

/// Result of `nav.status`.
struct NavStatusResult
{
    std::vector<NavStatusSurface> surfaces; ///< Every surface.
    u32 tiles = 0;                          ///< Tiles with polygons.
    u32 polygons = 0;                       ///< Polygons of every navmesh.
    u32 pendingTiles = 0;                   ///< Tiles waiting to be baked.
    u32 staleTiles = 0;                     ///< Loaded tiles whose input changed.
    u32 failures = 0;                       ///< Surfaces whose data or bake failed.
};

/// Params of `nav.path`.
struct NavPathParams
{
    Vec3 from{0.0f};                 ///< Start in world space, in m.
    Vec3 to{0.0f};                   ///< Destination in world space, in m.
    std::optional<u32> agentType{};  ///< Navmesh to search, 0 to 7 (default 0).
    std::optional<u32> areaMask{};   ///< Areas it may cross, bit i = area i (default all).
    std::optional<u32> maxCorners{}; ///< Corner points returned, 2 to 1024 (default 256).
};

/// Result of `nav.path`.
struct NavRouteResult
{
    bool partial = false;      ///< The path ends at the reachable point nearest the destination.
    f32 length = 0.0f;         ///< Length of the whole path in m.
    u32 total = 0;             ///< Corner points of the whole path.
    std::vector<Vec3> corners; ///< The first corner points, from the start on the navmesh.
};

/// Params of `nav.debug`.
struct NavDebugParams
{
    std::optional<bool> enabled{};   ///< Draw the navigation in the Scene views.
    std::optional<bool> navMesh{};   ///< Polygon edges, coloured by area.
    std::optional<bool> tiles{};     ///< Tile borders.
    std::optional<bool> links{};     ///< Off-mesh links.
    std::optional<bool> agents{};    ///< Agent cylinders and velocities.
    std::optional<bool> paths{};     ///< Corridors of moving agents.
    std::optional<bool> obstacles{}; ///< NavObstacles.
};

/// Result of `nav.debug`: what the Scene views draw now.
struct NavDebugResult
{
    bool enabled = false;  ///< The navigation is drawn.
    bool navMesh = true;   ///< Polygon edges.
    bool tiles = false;    ///< Tile borders.
    bool links = true;     ///< Off-mesh links.
    bool agents = true;    ///< Agents.
    bool paths = true;     ///< Corridors.
    bool obstacles = true; ///< NavObstacles.
};

/// Params of `ai.status`.
struct AiStatusParams
{
    std::optional<std::string> entity{}; ///< Entity reference of one agent (default: every agent).
    std::optional<bool> wait{};          ///< Finish loading the trees first (default true).
};

/// A blackboard key in the results of `ai.status` and `ai.setValue`.
struct AiStatusKey
{
    std::string name; ///< The key.
    std::string type; ///< "Bool", "Int", "Float", "Vector3" or "Entity".
    bool set = false; ///< Has a value.
    std::string
        value; ///< The value as text, e.g. "true", "3", "[1, 0, 2]" or an Entity's path ("" if unset).
};

/// What an agent perceives or remembers in the result of `ai.status`.
struct AiStatusPerceived
{
    EntitySummary entity; ///< What it perceives.
    std::string sense;    ///< "Sight" or "Hearing".
    Vec3 position{0.0f};  ///< Last known position in world space, in m.
    f32 age = 0.0f;       ///< Seconds since it was last sensed.
    bool current = false; ///< Sensed in the last update.
};

/// An agent in the result of `ai.status`.
struct AiStatusAgent
{
    EntitySummary entity;                     ///< The Entity.
    std::string tree;                         ///< Its `.behavior` file ("" for a perceiver without a tree).
    bool loaded = false;                      ///< Its tree has loaded.
    std::string running;                      ///< Name of the deepest running node ("" for none).
    u32 ticks = 0;                            ///< Ticks since the tree (re)started (at most 4294967295).
    std::vector<AiStatusKey> blackboard;      ///< Every key of its tree.
    std::vector<AiStatusPerceived> perceived; ///< What it perceives or remembers, nearest first.
};

/// Result of `ai.status`.
struct AiStatusResult
{
    std::vector<AiStatusAgent> agents; ///< The agents asked for, in Entity order (at most 100).
    u32 total = 0;                     ///< Entities with a BehaviorAgent or an AiPerception.
    u32 ticked = 0;                    ///< Trees ticked by the last update.
    u32 pending = 0;                   ///< Agents waiting for their tree.
    u32 failures = 0;                  ///< Trees that failed to load.
};

/// Params of `ai.setValue`.
struct AiSetValueParams
{
    std::string entity; ///< Entity reference of the agent.
    std::string key;    ///< Blackboard key of its tree.
    /// The value: true or false, a number, [x, y, z] or an Entity reference; null clears a Vector3 or Entity.
    JsonText value;
};

/// Result of `ai.setValue`.
struct AiSetValueResult
{
    EntitySummary entity; ///< The agent.
    AiStatusKey key;      ///< The key afterwards.
};

/// Params of `ai.noise`.
struct AiNoiseParams
{
    Vec3 position{0.0f};                     ///< Where it is made, in world space, in m.
    std::optional<f32> radius{};             ///< Distance in m at which hearing 1 hears it (default 10).
    std::optional<std::string> instigator{}; ///< Entity reference of who makes it (default none).
    std::optional<u32> team{};               ///< Team of the noise, 0 to 31 (default 0).
};

/// Result of `ai.noise`.
struct AiNoiseResult
{
    Vec3 position{0.0f}; ///< Where it is made.
    f32 radius = 10.0f;  ///< Its radius in m.
    u32 team = 0;        ///< Its team.
    u32 listeners = 0;   ///< Perceivers within its reach that notice its team (before the update's checks).
};

/// Params of `ai.validate`.
struct AiValidateParams
{
    std::string path; ///< A `.behavior` file of the assets: mount, e.g. "assets:/ai/guard.behavior".
};

/// Result of `ai.validate`.
struct AiValidateResult
{
    std::string path;      ///< The file.
    bool valid = false;    ///< It parses.
    std::string error;     ///< The first problem, with its JSON path ("" if valid).
    std::string errorPath; ///< The JSON path of the problem, e.g. "root.sequence[1].wait" ("" if none).
    u32 nodes = 0;         ///< Nodes of the tree (0 if invalid).
    u32 keys = 0;          ///< Blackboard keys of the tree (0 if invalid).
};

/// Params of `animation.list`.
struct AnimationListParams
{
    std::string path; ///< A model of the assets: mount, e.g. "assets:/models/hero.glb".
};

/// A skin (skeleton) of a model in the result of `animation.list`.
struct AnimationListSkin
{
    std::string ref;  ///< The reference for Animator::skeleton, e.g. "assets:/models/hero.glb#skin/0".
    std::string name; ///< The skin's name ("" if it has none).
    u32 joints = 0;   ///< Joints of its skeleton.
};

/// A clip of a model in the result of `animation.list`.
struct AnimationListClip
{
    std::string ref;     ///< The reference for Animator::clip, e.g. "assets:/models/hero.glb#animation/walk".
    std::string name;    ///< The clip's name.
    f32 duration = 0.0f; ///< Its length in seconds.
    u32 channels = 0;    ///< Animated properties of joints.
};

/// Result of `animation.list`.
struct AnimationListResult
{
    std::string path;                     ///< The model.
    std::vector<AnimationListSkin> skins; ///< Its skins, in order.
    std::vector<AnimationListClip> clips; ///< Its animations, in order.
};

/// Params of `animation.status`.
struct AnimationStatusParams
{
    std::optional<std::string> entity{}; ///< Entity reference of one Animator (default: every Animator).
};

/// A parameter of a graph in the results of `animation.status` and `animation.set_parameter`.
struct AnimationStatusParameter
{
    std::string name; ///< The parameter.
    std::string type; ///< "Float", "Int", "Bool" or "Trigger".
    f32 value = 0.0f; ///< Its value (Int and Bool as numbers; a Trigger is 1 while set).
};

/// A layer of a graph in the result of `animation.status`.
struct AnimationStatusLayer
{
    std::string name;          ///< The layer.
    std::string state;         ///< Its current (target) state.
    f32 normalizedTime = 0.0f; ///< Normalized time of the state (above 1 for a held non-looping state).
    std::string previous;      ///< State a running transition fades out ("" for none).
    f32 fade = 0.0f;           ///< Progress of the running transition in [0, 1] (0 without one).
};

/// An Animator in the result of `animation.status`.
struct AnimationStatusAnimator
{
    EntitySummary entity;     ///< The Entity.
    std::string skeleton;     ///< Animator::skeleton ("" for none).
    std::string clip;         ///< Animator::clip ("" for none).
    std::string graph;        ///< Animator::graph ("" for none).
    f32 speed = 1.0f;         ///< Animator::speed.
    f32 time = 0.0f;          ///< Animator::time, in seconds.
    bool loop = true;         ///< Animator::loop.
    bool playing = true;      ///< Animator::playing.
    bool posed = false;       ///< The animation runtime has posed it (always false without one).
    u32 joints = 0;           ///< Joints of its pose (0 while its assets load).
    bool graphLoaded = false; ///< Its graph has loaded and is bound to the skeleton.
    std::vector<AnimationStatusLayer> layers;         ///< The layers of its graph.
    std::vector<AnimationStatusParameter> parameters; ///< The parameters of its graph.
};

/// A state event in the result of `animation.status`.
struct AnimationStatusEvent
{
    EntitySummary entity; ///< The Entity of the Animator.
    std::string name;     ///< The event's name in the graph.
    std::string layer;    ///< The layer of the state.
    std::string state;    ///< The state.
};

/// Result of `animation.status`.
struct AnimationStatusResult
{
    std::string world; ///< "edit" or "play".
    bool live = false; ///< Poses, graph states and events are live (a play session); else saved fields only.
    std::string note;  ///< What is missing when not live ("" otherwise).
    std::vector<AnimationStatusAnimator>
        animators;    ///< The Animators asked for, in Entity order (at most 100).
    u32 total = 0;    ///< Entities with an Animator.
    u32 pending = 0;  ///< Animators waiting for their model (live only).
    u32 failures = 0; ///< Animators whose skeleton or clip could not be found (live only).
    std::vector<AnimationStatusEvent> events; ///< State events of the last update (live only, at most 64).
};

/// Params of `animation.set_parameter`.
struct AnimationSetParameterParams
{
    std::string entity; ///< Entity reference of the Animator.
    std::string name;   ///< Parameter of its graph (not a Trigger).
    JsonText value;     ///< true or false for a Bool, a whole number for an Int, a number for a Float.
};

/// Result of `animation.set_parameter`.
struct AnimationSetParameterResult
{
    EntitySummary entity;               ///< The Animator's Entity.
    AnimationStatusParameter parameter; ///< The parameter with the value written.
};

/// Params of `animation.trigger`.
struct AnimationTriggerParams
{
    std::string entity; ///< Entity reference of the Animator.
    std::string name;   ///< Trigger of its graph.
};

/// Result of `animation.trigger`.
struct AnimationTriggerResult
{
    EntitySummary entity; ///< The Animator's Entity.
    std::string name;     ///< The trigger set.
};

/// Params of `animation.scrub`.
struct AnimationScrubParams
{
    std::string entity; ///< Entity reference of the Animator.
    f32 time = 0.0f;    ///< Seconds into the clip (or the entry states of the graph), 0 to 1,000,000.
};

/// Result of `animation.scrub`.
struct AnimationScrubResult
{
    EntitySummary entity; ///< The Animator's Entity.
    f32 previous = 0.0f;  ///< Animator::time before.
    f32 time = 0.0f;      ///< Animator::time now.
    bool changed = false; ///< The time differs.
};

/// Params of `animation.validate`.
struct AnimationValidateParams
{
    std::string path; ///< An `.animgraph` or `.ragdoll` file of the assets: mount.
};

/// Result of `animation.validate`.
struct AnimationValidateResult
{
    std::string path;      ///< The file.
    std::string kind;      ///< "AnimationGraph" or "RagdollProfile".
    bool valid = false;    ///< It parses.
    std::string error;     ///< The first problem, with its JSON path ("" if valid).
    std::string errorPath; ///< The JSON path of the problem, e.g. "layers[0].states[2].motion" ("" if none).
    u32 parameters = 0;    ///< Parameters of a valid graph.
    u32 layers = 0;        ///< Layers of a valid graph.
    u32 states = 0;        ///< States of a valid graph, over all layers.
    u32 parts = 0;         ///< Parts of a valid ragdoll profile.
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

// --- gameui ------------------------------------------------------------------------------------------------

/// What `gameui.create` makes.
enum class GameUiKind : u8
{
    Canvas,     ///< A root that lays out its descendants on the screen (UiCanvas).
    Panel,      ///< A translucent rectangle (UiImage).
    Text,       ///< A line of text (UiText).
    Button,     ///< A background that reports Clicked, with a centred label child.
    Toggle,     ///< A box that switches on and off, with a check mark child.
    Slider,     ///< A bar with a value, with fill and handle children.
    TextField,  ///< A line of text the player edits.
    ScrollView, ///< A clipped area that scrolls a stack content child.
    Stack,      ///< Places its children one after another (UiStack).
    Grid,       ///< Places its children in rows of cells (UiGrid).
};

/// Params of `gameui.create`.
struct GameUiCreateParams
{
    GameUiKind kind = GameUiKind::Canvas; ///< What to create.
    /// Parent (default: the scene root for a canvas; otherwise the selected canvas or UI element, else the
    /// first canvas, created when there is none).
    std::optional<std::string> parent{};
    std::optional<std::string> name{}; ///< Name (default: after the kind, e.g. "Button").
    /// Text of a text or the label of a button, placeholder of a text field (default: after the kind).
    std::optional<std::string> text{};
};

/// A child that `gameui.create` made for its element.
struct GameUiPart
{
    std::string role{};     ///< "label", "check", "fill", "handle" or "content".
    EntitySummary entity{}; ///< The child.
};

/// Result of `gameui.create`.
struct GameUiCreateResult
{
    EntitySummary entity{};                ///< The created canvas or element.
    std::vector<GameUiPart> parts{};       ///< Its children, by role.
    std::optional<EntitySummary> canvas{}; ///< The canvas created for it because the scene had none.
};

/// Params of `gameui.layout`.
struct GameUiLayoutParams
{
    std::optional<std::string> entity{}; ///< A canvas or element: only it and its subtree (default: all).
    std::optional<u32> width{};          ///< Target width in pixels for the edit World, 1 to 16384 (1920).
    std::optional<u32> height{};         ///< Target height in pixels for the edit World, 1 to 16384 (1080).
};

/// A canvas in the result of `gameui.layout`.
struct GameUiCanvasInfo
{
    EntitySummary entity{};   ///< The canvas.
    i32 order = 0;            ///< Draw order: higher on top.
    bool modal = false;       ///< UiCanvas::modal.
    f32 pixelsPerUnit = 1.0f; ///< Target pixels per canvas unit.
    Vec2 size{0.0f};          ///< The whole target in canvas units.
    Vec2 areaMin{0.0f};       ///< Top left of the area its children are placed in, canvas units.
    Vec2 areaMax{0.0f};       ///< Bottom right of that area, canvas units.
};

/// An element in the result of `gameui.layout`.
struct GameUiElementInfo
{
    EntitySummary entity{};             ///< The element.
    std::string canvas{};               ///< Id of its canvas.
    Vec2 min{0.0f};                     ///< Top left in canvas units, before rotation and scale.
    Vec2 max{0.0f};                     ///< Bottom right in canvas units, before rotation and scale.
    Vec2 pixelMin{0.0f};                ///< Top left of its bounds on the target, in pixels.
    Vec2 pixelMax{0.0f};                ///< Bottom right of its bounds on the target, in pixels.
    f32 opacity = 1.0f;                 ///< Accumulated opacity.
    bool visible = true;                ///< Drawn: some opacity and not under a toggle that is off.
    std::optional<bool> interactable{}; ///< Has UiInteractable: whether it is enabled (absent otherwise).
    std::optional<bool> focused{};      ///< Play World: has the focus (absent for the edit World).
};

/// Result of `gameui.layout`.
struct GameUiLayoutResult
{
    std::string world{};                       ///< "edit" or "play".
    u32 width = 0;                             ///< Target width in pixels.
    u32 height = 0;                            ///< Target height in pixels.
    std::vector<GameUiCanvasInfo> canvases{};  ///< Canvases, bottom first.
    std::vector<GameUiElementInfo> elements{}; ///< Elements in draw order.
};

/// Params of `gameui.click`.
struct GameUiClickParams
{
    std::string entity{}; ///< The element to click.
};

/// One event of the game UI.
struct GameUiEventInfo
{
    /// "clicked", "value_changed", "focus_changed", "cancelled", "text_changed" or "submitted".
    std::string type{};
    std::optional<EntitySummary> entity{}; ///< The element (absent for a cancel without a canvas).
    f32 value = 0.0f;                      ///< New value of a toggle (0 or 1) or slider.
};

/// Result of `gameui.click`.
struct GameUiClickResult
{
    EntitySummary entity{};                ///< The element clicked.
    Vec2 pixels{0.0f};                     ///< Where, in pixels of the game's UI target.
    u32 updates = 0;                       ///< Updates the game ran (2, fewer when it ended).
    std::vector<GameUiEventInfo> events{}; ///< What the UI reported in those updates.
};

/// Params of `gameui.focus`.
struct GameUiFocusParams
{
    std::optional<std::string> entity{}; ///< The element to focus (null: clear the focus).
};

/// Result of `gameui.focus`.
struct GameUiFocusResult
{
    std::optional<EntitySummary> focused{}; ///< The element with the focus after the call.
};

/// Params of `gameui.pick`.
struct GameUiPickParams
{
    f32 x = 0.0f;                ///< Target pixel from the left.
    f32 y = 0.0f;                ///< Target pixel from the top.
    std::optional<u32> width{};  ///< Target width in pixels for the edit World, 1 to 16384 (1920).
    std::optional<u32> height{}; ///< Target height in pixels for the edit World, 1 to 16384 (1080).
};

/// Result of `gameui.pick`.
struct GameUiPickResult
{
    std::optional<EntitySummary> entity{}; ///< The topmost element at the pixel (absent for none).
    std::vector<EntitySummary> path{};     ///< Its canvas, then its ancestors down to it (empty for none).
};

/// Params of `gameui.place`.
struct GameUiPlaceParams
{
    std::string entity{};        ///< The UI element.
    Vec2 min{0.0f};              ///< Top left of its rectangle in canvas units.
    Vec2 max{0.0f};              ///< Bottom right of its rectangle in canvas units.
    std::optional<u32> width{};  ///< Target width in pixels for the edit World, 1 to 16384 (1920).
    std::optional<u32> height{}; ///< Target height in pixels for the edit World, 1 to 16384 (1080).
};

/// Params of `gameui.set_anchors`.
struct GameUiSetAnchorsParams
{
    std::string entity{};                   ///< The UI element.
    std::optional<UiAnchorPreset> preset{}; ///< A preset (or anchorMin and anchorMax).
    std::optional<Vec2> anchorMin{};        ///< Anchor of the top left corner, each axis 0 to 1.
    std::optional<Vec2> anchorMax{};        ///< Anchor of the bottom right corner, not below anchorMin.
    std::optional<bool> keepRect{};         ///< Change the offsets so the rectangle stays (default true).
    std::optional<u32> width{};             ///< Target width in pixels for the edit World, 1 to 16384 (1920).
    std::optional<u32> height{}; ///< Target height in pixels for the edit World, 1 to 16384 (1080).
};

/// Result of `gameui.place` and `gameui.set_anchors`: the element's UiElement afterwards.
struct GameUiPlacementResult
{
    EntitySummary entity{};      ///< The element.
    Vec2 anchorMin{0.0f};        ///< UiElement placement anchorMin.
    Vec2 anchorMax{0.0f};        ///< UiElement placement anchorMax.
    Vec2 offsetMin{0.0f};        ///< UiElement placement offsetMin, canvas units.
    Vec2 offsetMax{0.0f};        ///< UiElement placement offsetMax, canvas units.
    Vec2 size{0.0f};             ///< UiElement size, canvas units.
    bool placedByLayout = false; ///< Its parent's UiStack or UiGrid places it: only size was written.
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

/// Params of `export.build`.
struct ExportBuildParams
{
    /// Directory that receives the game's directory: absolute, or relative to the project root (default
    /// `.illusion/export/<platform>/out`).
    std::optional<std::string> output{};
    /// SDK configuration to build, e.g. "Release" (default: "Release" when the SDK has it, else the
    /// editor's).
    std::optional<std::string> configuration{};
    /// "none" (default), "archive" (ZIP; .tar.gz on Linux) or "installer" (NSIS on Windows, DMG on macOS).
    std::optional<std::string> package{};
    std::optional<bool> clean{};               ///< Discard the export's build tree first (default false).
    std::optional<std::string> linuxRuntime{}; ///< Steam Runtime to check against; not supported yet ("").
    /// Windows: command line run on the executable and the installer, `{file}` replaced by the path
    /// (default: env ILLUSION_SIGN_COMMAND; "" signs nothing).
    std::optional<std::string> signCommand{};
    /// macOS: codesign identity, "-" for ad hoc (default: env ILLUSION_CODESIGN_IDENTITY; "" signs nothing).
    std::optional<std::string> codesignIdentity{};
    /// macOS: notarytool keychain profile for the DMG (default: env ILLUSION_NOTARY_PROFILE; "" skips it).
    std::optional<std::string> notaryProfile{};
};

/// Result of `export.build`.
struct ExportBuildResult
{
    bool succeeded = false;      ///< Every step succeeded.
    std::string stage{};         ///< "done" after a success, otherwise the step that failed, e.g. "build".
    std::string error{};         ///< Why it failed ("" after a success).
    std::string platform{};      ///< "windows", "macos" or "linux".
    std::string configuration{}; ///< Build configuration of the executable.
    std::string directory{};     ///< The game's directory (file path).
    std::string package{};       ///< The archive or installer ("" for none).
    std::vector<ExportedFile> files{};           ///< Every file of the game's directory, sorted by path.
    u64 totalBytes = 0;                          ///< Sum of the files' sizes.
    bool signedBinaries = false;                 ///< The executable (and the installer) were signed.
    bool notarized = false;                      ///< macOS: the package was notarized.
    std::vector<std::string> skipped{};          ///< Asset files left out by the exclude patterns, sorted.
    u32 errors = 0;                              ///< Diagnostics with severity "error".
    u32 warnings = 0;                            ///< Diagnostics with severity "warning".
    std::vector<ModuleDiagnostic> diagnostics{}; ///< Diagnostics in output order (at most 200).
    std::string output{};                        ///< The last lines of the tools' output.
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

/// Params of `editor.reveal`.
struct RevealParams
{
    /// The file or folder: a virtual path such as "assets:/models/ship.glb", or an absolute path.
    std::string path{};
};

/// Result of `editor.reveal`.
struct RevealResult
{
    std::string file{};                   ///< The native path that was revealed (UTF-8).
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
    std::string type;                  ///< "Button", "Axis", "Vector2" or "Delta".
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
    std::string type;                                   ///< "Button", "Axis", "Vector2" or "Delta".
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

// --- gamepad -----------------------------------------------------------------------------------------------

/// One axis of a gamepad in `gamepad.list`.
struct GamepadAxisEntry
{
    std::string name; ///< "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger" or "RightTrigger".
    f32 value = 0.0f; ///< Raw value: sticks in [-1, 1] (+y up), triggers in [0, 1].
};

/// One touchpad finger of a gamepad in `gamepad.list`.
struct GamepadFingerEntry
{
    bool down = false;         ///< True while the finger touches the pad.
    Vec2 position{0.0f, 0.0f}; ///< Normalized position, (0, 0) top-left to (1, 1) bottom-right.
    f32 pressure = 0.0f;       ///< Pressure in [0, 1].
};

/// One connected gamepad.
struct GamepadEntry
{
    u32 id = 0;                              ///< Identifier, stable while it stays connected.
    std::string name;                        ///< Device name.
    std::string type;                        ///< Model, e.g. "PS5", "XboxOne" or "Generic".
    u32 player = 0;                          ///< Player slot (0-based).
    std::vector<std::string> features;       ///< "rumble", "trigger_rumble", "led", "motion", "touchpad", ...
    std::vector<std::string> buttons;        ///< Held buttons by position name, e.g. "South".
    std::vector<GamepadAxisEntry> axes;      ///< Every axis.
    std::vector<GamepadFingerEntry> fingers; ///< Touchpad fingers (empty without a touchpad).
    bool motion = false;                     ///< The motion sensors report.
    Vec3 gyroscope{0.0f};                    ///< Angular velocity in rad/s (x pitch, y yaw, z roll).
    Vec3 accelerometer{0.0f};                ///< Acceleration in m/s², gravity included.
};

/// Result of `gamepad.list`.
struct GamepadListResult
{
    std::vector<GamepadEntry> gamepads; ///< Connected gamepads, ordered by connection time.
};

/// Params of `gamepad.rumble`.
struct GamepadRumbleParams
{
    std::optional<u32> gamepad{};    ///< Gamepad id (default: the first connected).
    std::optional<f32> low{};        ///< Low-frequency motor in [0, 1].
    std::optional<f32> high{};       ///< High-frequency motor in [0, 1].
    std::optional<f32> left{};       ///< Left trigger motor in [0, 1] (Xbox One / Series).
    std::optional<f32> right{};      ///< Right trigger motor in [0, 1] (Xbox One / Series).
    std::optional<u32> durationMs{}; ///< How long, in ms (default 500, at most 10,000).
};

/// Params of `gamepad.set_led`.
struct GamepadLedParams
{
    std::optional<u32> gamepad{}; ///< Gamepad id (default: the first connected).
    Vec3 color{0.0f};             ///< Light colour, each channel in [0, 1].
};

/// Params of `gamepad.set_trigger_effect`.
struct GamepadTriggerEffectParams
{
    std::optional<u32> gamepad{};                          ///< Gamepad id (default: the first connected).
    GamepadTriggerSide trigger = GamepadTriggerSide::Left; ///< "left" (L2) or "right" (R2).
    GamepadTriggerMode mode = GamepadTriggerMode::Off;     ///< "off", "resistance", "weapon" or "vibration".
    std::optional<f32> start{};                            ///< Start position in [0, 1] (default 0).
    std::optional<f32> end{};                              ///< End position in [0, 1] (weapon; default 1).
    std::optional<f32> strength{};                         ///< Strength in [0, 1] (default 1).
    std::optional<f32> amplitude{};                        ///< Vibration amplitude in [0, 1] (default 1).
    std::optional<u32> frequency{}; ///< Vibration frequency in Hz, 1 to 255 (default 30).
};

/// Params of `gamepad.set_motion`.
struct GamepadMotionParams
{
    std::optional<u32> gamepad{}; ///< Gamepad id (default: the first connected).
    bool enabled = false;         ///< Turn the gyroscope and accelerometer on or off.
};

// --- saves -------------------------------------------------------------------------------------------------

/// One slot of the running game's saved games (`saves.list`).
struct SavesSlotEntry
{
    std::string name{};    ///< The slot name.
    std::string title{};   ///< What the game wrote as title ("" when the slot cannot be read).
    f64 playSeconds = 0.0; ///< Time played, in seconds.
    u32 gameVersion = 0;   ///< The game's own version number.
    std::string custom{};  ///< The game's own text.
    i64 savedAt = 0;       ///< When it was written, in seconds since the Unix epoch (UTC); 0 if unreadable.
    std::string state{};   ///< "Ok", "Backup", "Corrupt" or "NewerVersion".
    std::string screenshot{}; ///< Virtual path of the slot's image, or "".
};

/// Result of `saves.list`.
struct SavesListResult
{
    std::vector<SavesSlotEntry> slots{}; ///< The slots, sorted by name.
    bool busy = false;                   ///< A request has not delivered its event yet.
};

/// Params of `saves.write`.
struct SavesWriteParams
{
    std::string slot{};                  ///< The slot: 1 to 32 letters, digits, `_` and `-`.
    std::optional<std::string> title{};  ///< Text for the player, at most 128 bytes (default empty).
    std::optional<f64> playSeconds{};    ///< Time played in seconds, not negative (default 0).
    std::optional<u32> gameVersion{};    ///< A number the game gets back after a load (default 0).
    std::optional<std::string> custom{}; ///< Text the game defines, at most 1024 bytes (default empty).
};

/// Params of `saves.load` and `saves.remove`.
struct SavesSlotParams
{
    std::string slot{}; ///< The slot: 1 to 32 letters, digits, `_` and `-`.
};

/// Result of `saves.write`, `saves.load` and `saves.remove`.
struct SavesRequestResult
{
    std::string slot{};  ///< The slot of the request.
    std::string event{}; ///< The event that finished it: "Saved", "Loaded", "LoadedBackup" or "Removed".
};

} // namespace illusion::editor

/// Names of GameUiKind values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GameUiKind>
{
    /// The values.
    static constexpr std::array kValues{
        illusion::EnumValue{"canvas", 0},     illusion::EnumValue{"panel", 1},
        illusion::EnumValue{"text", 2},       illusion::EnumValue{"button", 3},
        illusion::EnumValue{"toggle", 4},     illusion::EnumValue{"slider", 5},
        illusion::EnumValue{"text_field", 6}, illusion::EnumValue{"scroll_view", 7},
        illusion::EnumValue{"stack", 8},      illusion::EnumValue{"grid", 9}};
};

/// Names of UiAnchorPreset values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::UiAnchorPreset>
{
    /// The values.
    static constexpr std::array kValues{
        illusion::EnumValue{"top_left", 0},        illusion::EnumValue{"top", 1},
        illusion::EnumValue{"top_right", 2},       illusion::EnumValue{"left", 3},
        illusion::EnumValue{"center", 4},          illusion::EnumValue{"right", 5},
        illusion::EnumValue{"bottom_left", 6},     illusion::EnumValue{"bottom", 7},
        illusion::EnumValue{"bottom_right", 8},    illusion::EnumValue{"stretch_top", 9},
        illusion::EnumValue{"stretch_middle", 10}, illusion::EnumValue{"stretch_bottom", 11},
        illusion::EnumValue{"stretch_left", 12},   illusion::EnumValue{"stretch_center", 13},
        illusion::EnumValue{"stretch_right", 14},  illusion::EnumValue{"stretch", 15}};
};

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

/// Names of GamepadTriggerSide values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GamepadTriggerSide>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"left", 0}, illusion::EnumValue{"right", 1}};
};

/// Names of GamepadTriggerMode values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::GamepadTriggerMode>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"off", 0}, illusion::EnumValue{"resistance", 1},
                                        illusion::EnumValue{"weapon", 2},
                                        illusion::EnumValue{"vibration", 3}};
};

/// Names of DropTarget values in JSON.
template <>
struct illusion::EnumReflection<illusion::editor::DropTarget>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"surfaces", 0}, illusion::EnumValue{"plane", 1},
                                        illusion::EnumValue{"colliders", 2}};
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
                                        illusion::EnumValue{"settings", 2}, illusion::EnumValue{"assets", 3}};
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
                                        illusion::EnumValue{"motionVectors", 8},
                                        illusion::EnumValue{"levelOfDetail", 9}};
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
