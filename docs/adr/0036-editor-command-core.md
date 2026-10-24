# 0036. Typed editor command core, transactions, operation-log undo, crash journal and project layout

- Status: Accepted
- Date: 2026-10-23

## Context

ADR 0030 made the editor command-first: panels only read state, and every change, whether it comes from a
human or an AI agent, is a command. Before any UI, RPC or MCP code (M7j–M7m) exists, the editor needs a core
that defines what a command is, how it is validated and undone, how changes are tracked and how work survives
a crash. The project brief requires that an agent can build a game through these commands without human
help, so they must be discoverable, strictly validated and safe to replay. Game modules run in-process
(M7 plan, decision 6), so a bad command must never reach an `ILL_ASSERT` or `ILL_CHECK`, and unsaved work
must be recoverable when something else crashes the process.

Reflection v2 (ADR 0032), persistent entity ids and scene format v2 (ADR 0033), the transform hierarchy
(ADR 0034) and asset references by path (ADR 0035) give every saved value a stable JSON form and every entity
a stable id.

## Decision

- **A headless library.** `illusion::editor_core` (editor only, ADR 0030) has no GPU, Dear ImGui or socket
  code. `EditorContext` owns the project's `VirtualFileSystem`, one edit World, `EntityIndex`, `Selection`,
  `UndoStack`, `ChangeJournal`, `EventHub`, `CommandRegistry`, the crash journal and autosave.
- **Typed commands.** A command is a handler `Expected<Result, CommandError>(CommandContext&, const Params&)`
  registered with a `CommandInfo` (name `domain.verb`, version, localized title and description keys, an
  English agent summary with an example, category, icon, flags, mode, permission class, palette visibility,
  default shortcut). Params and Result are structs reflected with `reflectStruct()`: they generate JSON
  Schema 2020-12 input and output schemas and convert JSON calls. `execute<Result>()` (C++) and
  `executeJson()` (RPC, MCP, tests) share one path. Every non-optional field is required; unknown members,
  wrong types and bad references are `validation_failed` errors with JSON Pointers and fuzzy suggestions.
  Errors use fixed application codes (1000–1500), outside the JSON-RPC and MCP ranges.
- **Entity references are strings**: an id, a hierarchy path of names (`#<id>` for unnamed segments), a
  unique `name:`, or `$selection`. Results return `{id, name, path}`; `Entity` handles never cross the API.
- **Transactions over primitive operations.** `WorldEditor` is the only writer. Each primitive validates
  first and records reversible operations: a Component value before and after (scene v2 JSON, entity fields
  as ids), entity snapshots that spawn or despawn (every reflected Component, runtime ones included), and
  file changes whose contents are blobs in `.illusion/trash/<session>`. Engine helpers such as `setParent()`
  run unchanged; the editor records the values they changed. Failed commands and dry runs roll back by
  applying the inverted operations. Destroying an entity nulls entity fields elsewhere that point into the
  subtree, so undo restores the ids and the references.
- **Operation-log undo.** An `UndoStep` holds the operations of one transaction with its label, origin,
  merge key and the selection before and after. Steps with the same merge key and origin merge (keeping the
  first "before" of each Component) until the stack is sealed by an undo, redo or save. The stack keeps at
  most 256 MiB and drops the oldest steps with an event. Saving records the undo state, which defines
  "dirty".
- **Change tracking.** Every commit, undo and redo makes a new scene revision; `ChangeJournal` keeps the
  entity changes of recent revisions for `scene.changes`-style resync, and `EventHub` delivers coalesced
  events once per frame.
- **Crash journal and recovery.** Every applied operation list is appended to
  `.illusion/journal/<session>.jsonl` (base scene, ops, saves) and flushed every frame; a clean shutdown
  deletes it. `project.recover` replays a leftover journal on top of its base scene as one undo step. Scenes
  whose ids were assigned on loading are embedded in the journal's base line. Dirty scenes are autosaved to
  `.illusion/autosave/` in rotating copies.
- **Projects.** A project is a directory with `project.illusion` (`illusion.project`: name, version, engine
  version, startup scene, modules), `assets/`, `settings/`, `modules/` and the git-ignored `.illusion/`
  (cache, journal, autosave, trash). It mounts `assets:`, `settings:`, `modules:` and `cache:` read-write.
  New projects are copies of `editor-data/templates/{empty,3d-starter}`.
- **The virtual file system manages files.** `VirtualFileSystem` gains `listRecursive`, `stat`,
  `createDirectory`, `remove`, `rename` and `copy` with `FileError::AlreadyExists` and `NotEmpty`; nothing is
  ever replaced implicitly, and paths stay confined to their mounts.
- **Generated reference.** `docs/content/{en,tr}/reference/editor-commands.md` is generated from the
  registry; the CTest `editor.command_reference` fails when it is stale, and a golden file of every command's
  schemas guards the protocol.

## Consequences

- Panels, shortcuts, RPC (M7j) and MCP (M7k) reuse one validated, undoable path; agents get schemas, typed
  errors and suggestions for free, and `dryRun`, `mergeKey` and `expectRevision` work for every command.
- Undo memory is proportional to what changed (JSON text), not to scene size, and undo is exact: undoing
  everything restores the scene byte for byte, which a seeded test of 2,000 random commands checks.
- Components without reflection cannot be recorded; entities restored by undo lose them. Engine and module
  Components that belong in scenes are reflected anyway (AGENTS.md).
- Commands do not nest and run on the owning thread at the command point; batches with `$` references,
  per-connection transactions, `clientToken`, `scope: mine` undo and permission enforcement come with the
  RPC layer (M7j).
- Scene settings, transforms with pivots and spaces, the clipboard, asset commands with a reference index,
  materials and log commands are separate modules (M7i-2), built on this core.
- Blobs of replaced and deleted files live until the session ends cleanly; a crashed session's blobs are
  kept with its journal so that recovery can replay file changes.

## Addendum: the M7i-2 command modules (2026-10-23)

These decisions add to the ones above; none of them changes them.

- **Derived world placements.** After every commit, undo, redo, recovery and scene load the editor derives
  the world `Transform` of every child from its `LocalTransform` (`EntityIndex::updateTransforms()`). It does
  not run `updateTransformHierarchy()`, which also writes the `LocalTransform` of roots: that value is saved,
  and a change made outside the operation log would make redo and journal replay diverge. Transform commands
  compute world placements through the chain of `LocalTransform`s, so they are right even within a command
  that moved a parent.
- **Settings.** `SettingsCatalog` registers reflected structs as scene settings (saved in the scene's
  `settings` section, changed through a new `SettingsChange` operation with the data versions before and
  after) or as project settings (one `illusion.settings` file per type under `settings:`, changed as file
  operations). Project settings cannot hold entity references. Scene settings of unregistered types are kept
  and saved again unchanged.
- **References.** `ReferenceIndex` finds asset references through reflection (`FieldType::Asset`) in scene
  and material files under `assets:/` and project settings files under `settings:/`, rereading every file and
  parsing again only files whose content hash changed (modification times can miss a same-length rewrite
  within their resolution). The open scene is searched in the edited World, and the manifest's startup scene
  counts as a reference (moving it is refused until the manifest can be edited through commands). Rewritten
  scenes are saved through the scene serializer, so they come out in the current format.
- **Clipboard.** The clipboard is scene v2 JSON of the copied subtrees, saved in the order given, with each
  copied root's world placement written into it; pasting loads it with new ids (`IdPolicy::Regenerate`).

## Addendum: transactions, batches, scoped undo and async commands (2026-10-24)

M7j-2 adds grouping to the command core without a second write path:

- **Transactions span commands.** `EditorContext::beginTransaction()` opens a transaction owned by a caller
  (`CommandOptions::caller`: an RPC connection id, or 0 for the editor itself). Each undoable command inside
  it still commits its own `WorldEditor` transaction, so a failing command rolls back alone, but its
  operations are appended to the open transaction instead of the history; `commitTransaction()` pushes them
  as one `UndoStep` (selection before and after the whole transaction), `cancelTransaction()` reverts them
  and `rollbackTransactionTo()` reverts to a savepoint. Every command inside still ends a revision and is
  journaled, so `expectRevision`, events and crash recovery behave as before. While a transaction is open,
  undoable commands of other callers fail with `busy` (retryable), and commands flagged
  `outsideTransactions` (scene replacement and saving, project open/create/recover, history moves) fail for
  everyone, because they would break the order of the operations. A transaction begun by a dry run is a
  trial: its changes stay pending, so the revision does not move and nothing is announced or journaled, and
  it can only be cancelled.
- **`editor.batch`** is a core command that runs its steps through `executeStep()` inside one transaction
  (its own, or a savepoint of the caller's open one), with `$name.path` references from earlier results. The
  call's `CommandGate` is asked before every step, which is how the RPC permission mode reaches nested
  commands.
- **Scoped undo.** `undoOwn()` undoes the last steps of one origin past later steps of other origins when
  `findOverlap()` finds them independent (no shared Component of an entity, no lifetime of an entity the other
  changes or refers to by id, no shared file path prefix or setting). Independent steps commute, so the stack
  moves the step to the top (`UndoStack::moveToTop()`) and undoes it normally; otherwise the call fails with
  `conflict` before changing anything. The check is conservative: references are found as quoted ids in
  Component values.
- **Async commands** register with `CommandRegistry::addAsync()`. Their handler validates and returns a
  `CommandTask` that the editor advances once per frame (`RunningCommand::update()`); destroying it cancels
  the command. They cannot be undoable yet, since a transaction cannot stay open across frames while others
  edit; asset imports and builds that edit the scene will finish with an ordinary undoable command.
- `CommandRegistry::revision()` grows with every registration; `endFrame()` posts `commands.changed`.
