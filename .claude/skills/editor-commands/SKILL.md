---
name: editor-commands
description: Add or change editor commands in illusion (editor/core): params and result structs, handlers, WorldEditor edits, undo, validation, the generated command reference and the schema golden file. Use whenever a command is added, renamed or its params/result change, or when code needs to change editor state.
---

# Editor commands

Read `docs/content/en/manual/editor/index.md` and ADR 0036 first. Every change of editor state (scene,
selection, files, history) is a command; panels, shortcuts, RPC and MCP all call them.

## 1. Where code goes

- Core modules: `editor/core/src/commands/<Domain>Commands.cpp`, registered from `registerCoreCommands()`
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
  (`EditorFixture::buildScene()`): the generic tests then run its success path against its output schema,
  check that a dry run changes nothing, and fuzz its params (`TestEditorFuzz.cpp`).
- Add a behaviour test for every error kind it can return and for undo/redo of what it changes.
- Regenerate the schema golden: `ILLUSION_UPDATE_GOLDENS=1 illusion_editor_tests -tc="the command catalogue*"`
  and review the diff. An incompatible change of params or result bumps the command's `version`.

## 4. Docs

- `illusion_editor_command_reference --write docs/content` rewrites the generated table in both
  `reference/editor-commands.md` pages; the CTest `editor.command_reference` fails until you do.
- Describe new concepts in `manual/editor/index.md` (EN and TR, identical code blocks).

## 5. Checklist

- [ ] Params validated before any change; fuzz test passes with no `internal` errors.
- [ ] Undoing the command restores the scene byte for byte (the random-command test covers core edits).
- [ ] Strings added, reference regenerated, catalogue golden updated, docs in both languages.
