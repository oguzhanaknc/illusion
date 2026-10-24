---
name: editor-commands
description: Add or change editor commands in illusion (editor/core): params and result structs, handlers, WorldEditor edits, undo, validation, the generated command reference and the schema golden file. Use whenever a command is added, renamed or its params/result change, or when code needs to change editor state.
---

# Editor commands

Read `docs/content/en/manual/editor/index.md` and ADR 0036 first. Every change of editor state (scene,
selection, files, history) is a command; panels, shortcuts, RPC and MCP all call them.

## 1. Where code goes

- Core modules: `editor/core/src/commands/<Domain>Commands.cpp` (`editor`, `project`, `scene`, `entity`,
  `component`, `hierarchy`, `transform`, `selection`, `history`, `clipboard`, `asset`, `material`, `log` and the
  scene and project `settings`), registered from `registerCoreCommands()`
  (`CoreCommands.cpp`). Params and result structs live in `editor/core/include/illusion/editor/core/
  CoreCommands.hpp` (each field with a `///` comment) and are reflected in `reflectCoreCommandTypes()`.
- Titles and descriptions: `editor.command.<name>.title` / `.description` in `editor/data/locales/en.json`
  (with a `comment`), used as `strings::kCommand<Domain><Verb>Title`.
- Primitive edits: `WorldEditor` (`editor/core/src/WorldEditor.cpp`). Add a primitive there when a command
  needs a change no primitive can express; never write to the World from a handler.

## 2. Writing a command

1. Name it `domain.verb` with a snake_case verb. Params: a struct whose optional fields are `std::optional`
   (everything else is required in the schema). Use `std::string` for entity references and paths,
   `JsonText` for free-form values (Component values, merge patches). Results return entities as
   `EntitySummary` (`context.summary(entity)`), never `Entity` bits.
2. Handler: `Expected<Result, CommandError> fn(CommandContext&, const Params&)`.
   - Resolve references with `context.entity(ref, "/pointer")`, `context.entities(list, "/pointer", out)` and
     `context.componentType(name, "/pointer")`: errors then carry the right JSON Pointer and suggestions.
   - Return `CommandError::invalid("/field", "must ...")` for bad values, `CommandError::make(kind, message)`
     otherwise. Messages are English, start lower case and name the thing. Never `ILL_ASSERT` on input.
   - Change things only through `context.edit()`; prefix the pointers of errors about a nested value with
     `withPointerPrefix(error, "/value")`.
   - Undoable commands need nothing else for dry runs (the transaction is rolled back). Other commands that
     change state (scene, selection, history) must check `context.dryRun()` and return before changing.
3. Register with `registry.add<&fn>({...})`: title and description keys, an English `agentSummary` ending
   in `Example: {...}.`, category, icon, flags (`readOnly` / `undoable` / `destructive` / `idempotent`),
   `mode`, `permission`, and `palette`/`defaultShortcut` for user-facing actions.

## 3. Tests (editor/tests/core)

- Add the command to `EditorCommandCases.hpp` with params valid on the fixture scene
  (`EditorFixture::buildScene()` and `addAssets()`, with the test settings types `test.Difficulty` and
  `test.Controls` registered and a full clipboard): the generic tests then run its success path against its
  output schema, check that a dry run changes nothing, and fuzz its params (`TestEditorFuzz.cpp`).
- Commands that change the scene belong in the random-command test (`TestEditorHistory.cpp`), which undoes and
  redoes 2,000 of them byte for byte.
- Add a behaviour test for every error kind it can return and for undo/redo of what it changes.
- Regenerate the schema golden: `ILLUSION_UPDATE_GOLDENS=1 illusion_editor_tests -tc="the command catalogue*"`
  and review the diff. An incompatible change of params or result bumps the command's `version`.

## 4. Docs

- `illusion_editor_command_reference --write docs/content` rewrites the generated table in both
  `reference/editor-commands.md` pages; the CTest `editor.command_reference` fails until you do.
- Describe new concepts in `manual/editor/index.md` (EN and TR, identical code blocks).

## 5. Patterns of the command modules

- **Placements** (`TransformCommands.cpp`): compute world placements with `worldTransformOf()` (the chain of
  `LocalTransform`s, right even after this command moved a parent), never from a child's `Transform`, which is
  derived. Write a child's `LocalTransform`, and for a root its `Transform` plus an equal `LocalTransform`, with
  `context.edit().assignComponent()`. Drop entities whose ancestor is also listed (`dropDescendants()`) before
  applying a delta.
- **Files** (`AssetCommands.cpp`, `MaterialCommands.cpp`): check the path first (mount, extension, `resolve()`),
  then change files only through `writeFile()`, `moveFile()`, `deleteFile()` and `createDirectory()` so undo and
  the journal see them. Commands that move or delete assets look references up with `context.editor().references()`
  plus `worldReferences()` for the open scene, and never touch the open scene's own file.
- **Settings** (`SettingsCommands.cpp`): look types up in `context.editor().settings()`; read the stored value
  into a default object, merge the patch with `readValue()`, and store it with `setSetting()` (scene) or
  `writeFile()` of `writeSettingsFile()` (project). Scene settings read entity fields through a
  `GuidEntityResolver`.
- **Editor state that is not undoable** (clipboard, log, selection): check `context.dryRun()` before changing it.

```json
{"entities": ["$selection"], "offset": [0, 1, 0], "space": "self", "mergeKey": "gizmo-drag"}
```

- **Transactions** (`TransactionCommands.cpp`): undoable commands need nothing for them; inside an open
  transaction their operations join it. Set `.flags = {.outsideTransactions = true}` on a command that replaces
  or saves the scene or moves through the history; such commands are refused while a transaction is open.
- **Batches** (`EditorCommands.cpp`): a command that runs others goes through `context.editor().executeStep()`
  with the caller's `origin`, `caller` and `gate`, inside `beginTransaction()` or a savepoint
  (`rollbackTransactionTo()`).
- **Async commands** (`editor.wait`): the handler validates and returns a `CommandTask<Result>` whose
  `update()` runs once per frame, fills `CommandProgress` and returns the result when done; register it with
  `registry.addAsync<&fn>({...})`. They cannot be undoable, and the generic tests run them through
  `test::run()`, which advances them to the end.

A new settings type needs no command: reflect the struct and register it.

```cpp
illusion::reflectStruct<game::Difficulty>("game.Difficulty").field("enemies", &game::Difficulty::enemies);
editor.settings().add("game.Difficulty", illusion::editor::SettingsScope::Scene);
```

## 6. Checklist

- [ ] Params validated before any change; fuzz test passes with no `internal` errors.
- [ ] Undoing the command restores the scene byte for byte (the random-command test covers core edits).
- [ ] Strings added, reference regenerated, catalogue golden updated, docs in both languages.
