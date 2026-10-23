# Editor Core

Header: `#include <illusion/editor/core/EditorContext.hpp>` (target `illusion::editor_core`, editor only)

The editor core is the editor without a window: the open project, the edited scene, the selection and
every command that changes them. Panels, shortcuts and the command palette call its commands, and AI
agents call the same commands over RPC and MCP (from M7j). Everything works headless, which is how the
tests drive it.

## The editor context

`EditorContext` owns a `VirtualFileSystem` with the [project mounts](projects.md), one World with the open
scene, the selection, the undo history, the journals and the command registry:

```cpp
using namespace illusion::editor;

EditorContext editor(EditorContextOptions{.seed = 42}); // seeded ids for reproducible sessions
if (auto opened = editor.openProject("C:/Games/Tetris"); !opened)
{
    ILL_LOG_ERROR("Editor", "{}", opened.error().message);
}

EntityCreateParams params;
params.name = "Board";
auto created = editor.execute<EntityResult>("entity.create", params);
// created->entity.id is the new EntityGuid as 16 hex digits, created->entity.path "/Board"

editor.endFrame(1.0 / 60.0); // events, crash journal, autosave
```

Commands run on the thread that owns the context, at the command point of the frame, never while a Query
iterates. A command cannot run another command.

## Commands

Every command has a descriptor: a `domain.verb` name, a version, a localized title and description, an
English summary for agents with an example call, a category, an icon, flags, a permission class and an
optional default shortcut. Its params and result are structs reflected with `reflectStruct()`, which give
its JSON Schemas (2020-12). The [command reference](../../reference/editor-commands.md) lists them all.

There are two ways to run one, and both take the same path:

| Call | Used by |
| --- | --- |
| `execute<Result>(name, params, options)` | C++ code: panels, gizmos, tools |
| `executeJson(name, json, options)` | RPC, MCP, scripts and tests; the result is JSON with an `_editor` member |

```json
{"name": "Board", "parent": "/Game", "components": {"illusion.Transform": {"position": [4.5, 9.5, 0]}}}
```

JSON params are checked before anything runs: every field without `std::optional` is required, unknown
members are errors (with the closest field names as suggestions), and each error carries a JSON Pointer
to the value, such as `/components/illusion.Transform/position`. Every command that is not read-only also
takes these options:

| Option | Meaning |
| --- | --- |
| `dryRun` | Validate and report how many operations it would make, without changing anything. |
| `mergeKey` | Continuous edits (a drag) with the same key and origin become one undo step. |
| `expectRevision` | Fail with `conflict` unless the scene is at this revision. |

## Entity references

Commands take entities as strings and return them as `{id, name, path}`; raw `Entity` handles never cross
the API.

| Form | Example |
| --- | --- |
| 16 hex digits: an id | `9f2c41aa0b7d3e15` |
| Hierarchy path of names | `/Game/Board/Cell 3` |
| `name:` and a name only one entity has | `name:Board` |
| The selection, when exactly one entity is selected | `$selection` |

A path segment of an entity without a name is `#` and its id. Lists of entities take `$selection` for every
selected entity. Names that match more than one entity give `ambiguous`; misspelt ones list close matches.

## Values

Component values use the scene v2 encoding: vectors are arrays, quaternions `[x, y, z, w]`, enums names,
entity fields ids, asset fields virtual paths, u64 values decimal strings. Fields with a unit also accept a
tagged number such as `{"deg": 90}`, and colour fields `"#rrggbb"`. `component.set` merges: fields it does
not mention keep their values.

## Transactions and undo

Undoable commands run in a transaction. Each primitive edit validates first and records reversible
operations: Component values before and after, snapshots of entities that appear or disappear, and file
changes. A command that fails, and every dry run, is rolled back; a committed one becomes an undo step with
a label, an origin (`ui`, `inspector`, `gizmo`, `rpc:<client>`, ...) and the selection before and after.

- Destroying an entity destroys its descendants and sets entity fields elsewhere that point at them to
  null; undo brings back the same ids and the references.
- Undoing everything gives the scene back byte for byte, and redoing everything gives the final scene.
- The history keeps at most 256 MiB (`EditorContextOptions::undoBudget`); older steps are dropped with a
  `history.trimmed` event.
- Selecting is not an undo step, but undo and redo restore the selection of their step.
- Saving remembers the undo state, so undoing back to it makes the scene clean again.

## Errors

Failures return a `CommandError` with a kind, an English message, the invalid values and suggestions. The
kinds have fixed codes, outside the JSON-RPC and MCP ranges:

| Code | Kind | Code | Kind |
| --- | --- | --- | --- |
| 1000 | `unauthenticated` | 1203 | `conflict` |
| 1001 | `permission_denied` | 1204 | `ambiguous` |
| 1002 | `busy` | 1205 | `precondition_failed` |
| 1003 | `timeout` | 1300 | `io_error` |
| 1004 | `cancelled` | 1301 | `build_failed` |
| 1100 | `wrong_mode` | 1302 | `module_error` |
| 1200 | `not_found` | 1400 | `unsupported` |
| 1201 | `already_exists` | 1401 | `limit_exceeded` |
| 1202 | `validation_failed` | 1500 | `internal` |

## Changes and events

Every commit, undo and redo increases the scene revision. `changes().changesSince(revision, out)` returns
which entities were created, destroyed or changed (and which Components), and which scene settings changed,
since a revision a client remembers, or `false` when it must read the scene again. `events()` delivers `scene.changed`,
`selection.changed`, `history.changed`, `history.trimmed`, `scene.opened`, `scene.saved` and
`scene.autosaved` once per `endFrame()`.

## Crash journal and recovery

While a project is open, every applied change is appended to `.illusion/journal/<session>.jsonl` and
flushed at the end of each frame. A clean shutdown deletes the file, so a journal that is left behind
belongs to a session that crashed. `project.recover` (or `EditorContext::recover()`) opens the scene that
journal started from and replays it as one undo step. A dirty scene is also autosaved every 60 seconds to
`.illusion/autosave/`, keeping three rotating copies.

## Transforms

The `transform` commands place entities without writing Component values by hand:

| Command | What it does |
| --- | --- |
| `transform.get` | The placement in world space or relative to the parent, with the rotation also as Euler angles in degrees. |
| `transform.set` | Sets position, rotation and scale in world space (default) or relative to the parent. |
| `transform.translate` | Moves by an offset along the world axes, the parent's axes (`local`) or the entity's own (`self`). |
| `transform.rotate` | Turns about each entity's own position, about their `center` or about a `point`. |
| `transform.scale` | Multiplies the scale; with a pivot the positions spread from it too. |
| `transform.reset` | Resets position, rotation and scale relative to the parent. |

Rotations are quaternions `[x, y, z, w]` (`rotation`) or Euler angles in degrees (`euler`, applied as yaw,
then pitch, then roll, like `quatFromEuler()`). For a child the commands write its `LocalTransform` (exactly,
when the space is `local`) and its derived `Transform`; for a root they write `Transform` and keep its
`LocalTransform` equal. An entity listed together with one of its ancestors moves once, with the ancestor.

```json
{"entities": ["/Game/Board", "/Game/Player"], "euler": [0, 90, 0], "pivot": "center"}
```

After every change, and after a scene opens, the editor derives the world `Transform` of every child from
its `LocalTransform` (`EntityIndex::updateTransforms()`), so world placements are current for the next
command: `hierarchy.set_parent` with `keepWorld` keeps an entity where it is even right after loading. Only
derived values are written there; saved values change only through commands, so undo stays exact.

## Clipboard

`clipboard.copy` puts entities and their descendants on the editor's clipboard as scene v2 JSON and returns
it; copied roots keep their world placement. `clipboard.paste` creates them again with new ids, under a
`parent` if one is given, and selects them. Entity fields between the copied entities point at the new
copies; fields that pointed outside the copy become null. `clipboard.cut` copies and destroys in one undo
step. `clipboard.paste` also takes scene JSON in `text`, so agents can insert prepared entities.

## Assets and references

The `asset` commands work on files under `assets:/`: `asset.list` (with the kinds `Folder`, `Scene`,
`Material`, `Model`, `Texture`, `Environment`, `AudioClip` and `Other`), `asset.info`,
`asset.create_folder`, `asset.move`, `asset.delete`, `asset.references` and `asset.dependencies`.

References are found through reflection: every `AssetRef` field of a saved Component, of the scene's
settings, of `.material` files and of project settings files. `ReferenceIndex` reads every scene and
material under `assets:/` and every settings file under `settings:/` (extensions in any case), and parses
again only the files whose contents changed. The open scene is read from the edited World instead of its
file, so unsaved changes count, and the manifest's startup scene counts as a reference from
`project.illusion`. A reference to `assets:/models/ship.glb` also matches its sub-assets (`#mesh/0`), and a
folder matches everything inside it.

- `asset.move` moves a file or folder and rewrites every reference to it, in closed scenes, materials and
  settings files (written again) and in the open scene (saved with it), as one undo step.
- `asset.delete` refuses while something outside the deleted path refers to it; with `force` it deletes
  anyway and returns the dangling references. Undo brings the files back.
- Neither command moves or deletes the open scene's file: save the scene under another name first. The
  startup scene cannot be moved either.

Components and settings whose types are not registered (kept as they are in the file) are not searched;
`asset.references` warns about files it cannot read.

## Materials

`material.create`, `material.get` and `material.set` read and write `.material` files through the
reflection of `MaterialAsset`: the fields of `MaterialData` under `material`, texture paths under
`textures`. `material.set` merges, and both writing commands are undoable file changes.

```json
{"path": "assets:/materials/red.material", "value": {"material": {"baseColorFactor": "#cc2222", "roughnessFactor": 0.4}}}
```

## Settings

Settings types are reflected structs registered in `EditorContext::settings()`, each for scenes or for the
project:

```cpp
illusion::reflectStruct<game::Difficulty>("game.Difficulty").field("enemies", &game::Difficulty::enemies);
editor.settings().add("game.Difficulty", illusion::editor::SettingsScope::Scene);
editor.settings().add("game.Controls", illusion::editor::SettingsScope::Project, "settings:/controls.json");
```

- **Scene settings** live in the `settings` section of the scene file. `scene.settings_get` lists every
  registered type with all its fields (defaults when the scene does not set it) and settings of unknown
  types as they are saved; `scene.settings_set` merges a patch as an undo step (`replace` resets the other
  fields, `null` removes the setting). Settings of unknown types are saved again unchanged.
- **Project settings** live in their own file under `settings:`, in the format `illusion.settings` with the
  type and its data version, so older files migrate. `project.settings_get` and `project.settings_set` work
  the same way; `null` deletes the file. Project settings cannot hold entity references.

## Log

The editor keeps the newest log messages of the process (`EditorContextOptions::logCapacity`, 1,024 by
default). `log.read` returns them oldest first, filtered by `minLevel`, `category` or `contains`: without
`since` the newest `limit` of them, with `since` the oldest after it, so that passing the returned `last` as
`since` again reads on without losing any. `log.clear` hides what was logged so far, and
`log.set_level` sets the lowest level the process logs.

## Writing a command

A handler is a function of the params struct that returns the result struct or an error. It reads the
World and changes it only through `CommandContext::edit()`:

```cpp
struct NudgeParams
{
    std::string entity;
    f32 distance = 0.0f;
};

Expected<EntityResult, CommandError> nudge(CommandContext& context, const NudgeParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    const std::string value = "{\"position\":[" + std::to_string(params.distance) + ",0,0]}";
    const JsonDocument patch = std::move(JsonDocument::parse(value).value());
    if (auto moved = context.edit().setComponent(*entity, componentId<Transform>(), patch.root()); !moved)
    {
        return unexpected(std::move(moved).error());
    }
    return EntityResult{context.summary(*entity)};
}

reflectStruct<NudgeParams>("game.NudgeParams").field("entity", &NudgeParams::entity).field("distance", &NudgeParams::distance);
editor.commands().add<&nudge>({.name = "game.nudge", .title = strings::kNudgeTitle, .description = strings::kNudgeDescription,
                               .agentSummary = "Moves an entity along x. Example: {\"entity\": \"/Board\", \"distance\": 1}.",
                               .category = "Game", .flags = {.undoable = true}, .permission = PermissionClass::Edit});
```
