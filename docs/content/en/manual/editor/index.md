# Editor Core

Header: `#include <illusion/editor/core/EditorContext.hpp>` (target `illusion::editor_core`, editor only)

The editor core is the editor without a window: the open project, the edited scene, the selection and
every command that changes them. Panels, shortcuts and the command palette call its commands, and AI
agents call the same commands over [RPC](automation.md) (and MCP from M7k). Everything works headless, which is how the
tests drive it.

## Component clipboard and view state

`component.copy` copies one Component into a separate in-memory clipboard, preserving Entity references
as persistent ids. `component.paste` applies it to every target in one undo step; missing Components and
their requirements are added together. A failure rolls back the entire paste. The entity clipboard is
independent. Neither copying nor changing view state adds an undo step.

`component.reset` accepts an optional `path` to reset only one field. Component command paths use `/`
between fields and array indices; vector axes also accept `x`, `y`, `z` and `w`.

```json
{"method":"component.copy","params":{"entity":"/Light","type":"illusion.Transform"}}
{"method":"component.paste","params":{"entities":["$selection"]}}
{"method":"component.reset","params":{"entity":"/Light","type":"illusion.Transform","path":"position/x"}}
{"method":"view.set_entities","params":{"entities":["/Game"],"hidden":true,"locked":true}}
```

`view.get` lists hidden and locked entities. `view.set_entities` changes either flag, including descendants
unless `recursive` is false. This state belongs to the current editor session, stays out of scene files
and clears when the scene is replaced. Dry runs leave both clipboards and view state unchanged.

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
| `clientToken` | An idempotency key: over [RPC](automation.md), repeating a token returns the first reply instead of running the command again. |

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

### Grouping changes

`transaction.begin {"label": "Build board"}` opens a transaction: the following undoable commands of the same
caller apply at once but become a single undo step when `transaction.commit` runs, while
`transaction.cancel` reverts all of them and restores the selection. A transaction belongs to its caller
(`CommandOptions::caller`, the connection for RPC clients; 0 for the editor itself). While it is open:

- undoable commands of other callers fail with `busy` (1002, `retryable`); reading and selecting still work;
- commands that replace or save the scene or move through the history (`scene.new`, `scene.open`,
  `scene.save`, `scene.save_as`, `project.open`, `project.create`, `project.recover`, `history.*` except
  `history.list`; flag `outsideTransactions`) fail for everyone;
- each command still increases the revision, so `expectRevision` works inside it;
- when its RPC connection closes, the editor cancels it.

### Batches

`editor.batch` runs a list of commands in order as one transaction and undo step. A step named with `as`
can be used by later steps: the string `"$name.path.to.field"` anywhere in their params becomes that value
of its result (array elements by index; `"$$"` escapes a literal `$`, and `"$selection"` keeps its meaning).

```json
{"label": "Board", "steps": [
  {"as": "board", "method": "entity.create", "params": {"name": "Board", "parent": "/Game"}},
  {"method": "entity.create", "params": {"name": "Cell 1", "parent": "$board.entity.id"}},
  {"method": "selection.set", "params": {"entities": ["$board.entity.path"]}}
]}
```

Every step is checked before any runs (method names, `as` names, params objects); async commands,
`transaction.*` and nested batches cannot be steps. An atomic batch (the default) reverts everything when a
step fails and returns that step's error with its pointer under `/steps/<i>/params`; with `"atomic": false`
failing steps are reported in `steps[i].error` and the others are kept. A batch inside an open transaction of
the same caller joins it, and a failing atomic batch reverts only its own steps. A dry run of a batch runs
the undoable steps in a trial transaction (`TransactionInfo::trial`) that is cancelled at the end without
changing the revision, and only validates the steps that are neither undoable nor read-only. The steps run
with the gate of the call (`CommandOptions::gate`), so the RPC permission mode applies to each of them.

### Undoing your own steps

`history.undo {"scope": "mine"}` undoes the last steps of the caller's origin even when steps of other
origins came after them, as long as those later steps are independent (`findOverlap()`): they did not change
the same Component of an entity, create or destroy an entity the step changed or refers to, or touch the same
file or setting. Otherwise it fails with `conflict` (1203) and changes nothing. The undone step moves to the
redo side, so `history.redo` applies it again.

### Async commands

Commands that take several frames are registered with `CommandRegistry::addAsync()`: their handler validates
the params and returns a `CommandTask`, which the editor advances once per frame with a `CommandProgress`.
`executeJson()` refuses them; `startJson()` returns a `RunningCommand` to advance with `update()` and to
destroy to cancel. Async commands cannot be undoable. `editor.wait {"frames": 60}` is the first one.

```cpp
const illusion::JsonDocument params = std::move(illusion::JsonDocument::parse(R"({"frames": 60})").value());
auto started = editor.startJson("editor.wait", params.root());
if (started)
{
    std::unique_ptr<illusion::editor::RunningCommand> running = std::move(started).value();
    // Once per frame, on the editor's thread:
    auto step = running->update(); // std::nullopt while it runs; the result JSON or an error at the end
}
```

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
`scene.autosaved` once per `endFrame()`, `commands.changed` (`{"commands": <count>}`) at the end of a frame
in which commands were registered, and `asset.changed` when the asset catalog changed (see below).

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
| `transform.snap_to_grid` | Rounds world positions to the nearest multiple of a grid step (the gizmo's move step by default). |

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
`asset.create_folder`, `asset.move`, `asset.delete`, `asset.references`, `asset.dependencies` and
`asset.find_missing`.

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

### The asset catalog

`AssetCatalog` (`editor.assets()`) knows every file and folder under `assets:/`: its kind, size,
modification stamp, xxHash64 content hash and dependencies (the references of scenes and materials, and the
external buffers and images of `.gltf` files), and which files refer to it (`referrers()`). The editor
watches the project's `assets/` directory with a `FileWatcher` (Native by default,
`EditorContextOptions::assetWatching`), so changes made outside the editor, by an image editor or by
`git checkout`, are seen too:

- Commands bring the catalog up to date before they use it, reading only files whose size or stamp
  changed, and every file the watcher reported.
- `endFrame()` checks the watcher every `assetPollInterval` seconds (0.25 by default) once the catalog has
  been used, and posts `asset.changed` with what changed since the last one:
  `{"added": [...], "modified": [...], "removed": [...], "renamed": [{"from": ..., "to": ...}],
  "reimported": [...]}`.
- A file that disappears and reappears elsewhere with the same contents (a rename in a file manager) is
  reported in `renamed`; references to its old path are not changed. `asset.move` moves files and rewrites
  references.
- **Live reload.** The viewports reload every file in `asset.changed`, and the files that refer to it (the
  glTF file of a changed buffer or image), on their next frame: a material saved in another program shows
  its new colours, a reimported model replaces the old one in the edit and play Worlds, and an asset that
  was missing appears once its file exists. Assets no World uses any more are freed from the GPU then and
  when a play session stops.

`asset.find_missing` lists every reference to an `assets:` path that does not exist (in scenes, materials,
settings files, the open scene, the manifest and glTF files), with the entity, Component and field of each.

### Import settings

Textures and models keep their [import settings](../assets.md#import-settings) in a `<file>.import.json`
sidecar next to them. The editor treats the sidecar as part of its asset:

- `asset.set_import_settings` merges settings into the sidecar (a JSON merge patch: `null` resets a
  setting, a section or, as the whole value, everything). It writes the sidecar only while a setting
  differs from its default and deletes it when all are defaults again; each call is one undo step. The
  result has every setting, defaults included, and `asset.info` returns them as `importSettings`:

```json
{"path": "assets:/textures/ui.png", "value": {"texture": {"colorSpace": "linear", "maxSize": 512}}}
```

- Changing a sidecar, from the command, by undo or in a text editor, reports its asset in the `modified`
  list of `asset.changed`; the viewports reload it, and the import cache entry, whose sources include the
  sidecar, imports again.
- `asset.reimport` imports a texture or model, or every one in a folder, again on the next frame even
  though nothing changed: it posts them in `reimported`, and the viewports reload them with
  `AssetReloadMode::Reimport` (with the glTF models that use a reimported image).
- `asset.list` and the catalog do not list sidecars. `asset.move` moves a file's sidecar with it and
  `asset.delete` deletes it, in the same undo step; a move to a name that could not use the settings (a
  texture renamed to `.txt`) is refused until they are reset. Sidecars are not valid paths for these
  commands themselves.

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
