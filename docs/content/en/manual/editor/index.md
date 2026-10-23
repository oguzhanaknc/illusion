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
which entities were created, destroyed or changed (and which Components) since a revision a client
remembers, or `false` when it must read the scene again. `events()` delivers `scene.changed`,
`selection.changed`, `history.changed`, `history.trimmed`, `scene.opened`, `scene.saved` and
`scene.autosaved` once per `endFrame()`.

## Crash journal and recovery

While a project is open, every applied change is appended to `.illusion/journal/<session>.jsonl` and
flushed at the end of each frame. A clean shutdown deletes the file, so a journal that is left behind
belongs to a session that crashed. `project.recover` (or `EditorContext::recover()`) opens the scene that
journal started from and replays it as one undo step. A dirty scene is also autosaved every 60 seconds to
`.illusion/autosave/`, keeping three rotating copies.

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
