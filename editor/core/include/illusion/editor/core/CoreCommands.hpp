#pragma once

/// @file
/// The core command modules (`editor`, `project`, `scene`, `entity`, `component`, `hierarchy`,
/// `selection`, `history`) and their params and result structs, for typed calls from the UI. The
/// generated reference lists every command: docs/content/en/reference/editor-commands.md.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/editor/core/EntityIndex.hpp>

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

/// Params of `component.remove`, `component.get` and `component.reset`.
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

// --- history -----------------------------------------------------------------------------------------------

/// Params of `history.undo` and `history.redo`.
struct HistoryStepParams
{
    std::optional<u32> steps{}; ///< Number of steps (default 1).
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

} // namespace illusion::editor
